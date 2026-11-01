// lib/detail/strnum.hpp — leaf token scanning: string / integer / float /
// bool / datetime.
//
// Private header: included by parser.cpp only. Arrays and inline tables are
// document structure (parser.cpp recurses over them); this file handles the
// complete consumption of a single value token and the construction of its
// typed value. Every failure goes through fail() as a parse_abort.
#pragma once

#include "charset.hpp"
#include "rfc3339.hpp"
#include "scanner.hpp"

#include <cpptoml/value.hpp>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace cpptoml::detail::strnum {

// ---- Shared character predicates --------------------------------------------

/// End of a value token: whitespace, a newline, EOF, or a comment start.
inline bool at_delimiter(const scanner& s) {
    return s.eof() || charset::is_ws(s.peek()) || s.at_newline() || s.peek() == '#';
}

// ---- UTF-8 output ------------------------------------------------------------

inline void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// Read n hex digits; too few, invalid, or a surrogate is an error.
inline std::uint32_t take_hex_escape(scanner& s, int n) {
    std::uint32_t cp = 0;
    for (int i = 0; i < n; ++i) {
        if (s.eof() || !charset::is_hex_digit(s.peek())) {
            fail(s, "the \\u escape requires " + std::to_string(n) + " hexadecimal digits");
        }
        cp = cp * 16 + static_cast<std::uint32_t>(charset::hex_value(s.peek()));
        s.bump();
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        fail(s, "the \\u escape code point is a surrogate (not allowed)");
    }
    if (cp > 0x10FFFF) {
        fail(s, "hexadecimal escape code point beyond the Unicode range");
    }
    return cp;
}

// ---- Escapes -----------------------------------------------------------------

/// Handle the escape after a backslash and return the expanded bytes; s
/// rests on the backslash.
inline std::string take_escape(scanner& s) {
    s.bump();  // backslash
    if (s.eof()) fail(s, "unterminated string (the escape runs to end of file)");
    const char c = s.peek();
    switch (c) {
        case 'b': s.bump(); return std::string(1, '\b');
        case 't': s.bump(); return std::string(1, '\t');
        case 'n': s.bump(); return std::string(1, '\n');
        case 'f': s.bump(); return std::string(1, '\f');
        case 'r': s.bump(); return std::string(1, '\r');
        case '"': s.bump(); return std::string(1, '"');
        case '\\': s.bump(); return std::string(1, '\\');
        case 'u': s.bump(); break;
        case 'U': s.bump(); break;
        default:
            fail(s, std::string("invalid escape '\\") + c + "'");
    }
    const int n = (c == 'u') ? 4 : 8;
    const std::uint32_t cp = take_hex_escape(s, n);
    std::string out;
    append_utf8(out, cp);
    return out;
}

// ---- Single-line strings -----------------------------------------------------

/// Single-line basic string: s rests on the opening quote; no LF inside,
/// full escapes (\b\t\n\f\r\"\\ \u \U).
inline std::string scan_basic_string(scanner& s) {
    s.bump();  // opening quote
    std::string out;
    for (;;) {
        if (s.eof() || s.peek() == '\n') fail(s, "unterminated string (no closing quote before end of line)");
        if (s.peek() == '"') { s.bump(); return out; }
        if (s.peek() == '\\') {
            out += take_escape(s);
            continue;
        }
        out.push_back(s.peek());
        s.bump();
    }
}

/// Single-line literal string: s rests on the opening quote; no escapes,
/// backslashes stay verbatim.
inline std::string scan_literal_string(scanner& s) {
    s.bump();  // opening quote
    std::string out;
    for (;;) {
        if (s.eof() || s.peek() == '\n') fail(s, "unterminated string (no closing quote before end of line)");
        if (s.peek() == '\'') { s.bump(); return out; }
        out.push_back(s.peek());
        s.bump();
    }
}

// ---- Multiline strings -------------------------------------------------------

/// Multiline string: s rests on the opening triple-quote. basic selects the
/// escaped form; false selects the escape-free literal form.
inline std::string scan_multiline_string(scanner& s, bool basic) {
    const char q = basic ? '"' : '\'';
    s.bump_n(3);  // opening triple-quote

    std::string out;
    // A newline right after the opener is trimmed (only if truly present).
    if (s.at_newline()) s.bump_newline();

    for (;;) {
        if (s.eof()) fail(s, "unterminated string (the multiline string never finds its closing triple quote)");
        const char c = s.peek();

        // Closing-quote run: k-3 go into the content + 3 close the string;
        // k > 5 is unrepresentable.
        if (c == q) {
            const std::size_t k = s.run_length(q);
            if (k >= 3) {
                if (k > 5) fail(s, "a closing-quote run longer than 5 inside the string");
                s.bump_n(k - 3);
                s.bump_n(3);
                return out;
            }
            out.append(k, q);
            s.bump_n(k);
            continue;
        }

        if (basic && c == '\\') {
            // Line-ending backslash: only whitespace may follow \ and a
            // newline must be reached; all whitespace/newlines are then
            // swallowed in one go.
            std::size_t j = 1;
            while (s.peek_at(j) == ' ' || s.peek_at(j) == '\t') ++j;
            const char after = s.peek_at(j);
            if ((after == '\n') || (after == '\r' && s.peek_at(j + 1) == '\n')) {
                s.bump_n(j + 1);       // backslash + spaces + first newline byte
                if (s.peek() == '\n') s.bump();  // LF of the CRLF
                while (!s.eof() && (charset::is_ws(s.peek()) || s.at_newline())) {
                    if (s.at_newline()) s.bump_newline();
                    else s.bump();
                }
                continue;
            }
            out += take_escape(s);
            continue;
        }

        if (c == '\r') { s.bump(); continue; }  // CR of a CRLF: emit LF only
        if (c == '\n') { out.push_back('\n'); s.bump(); continue; }

        out.push_back(c);
        s.bump();
    }
}

// ---- Bools --------------------------------------------------------------------

inline bool scan_bool(scanner& s) {
    const std::string_view word = (s.peek() == 't') ? "true" : "false";
    for (const char w : word) {
        if (s.eof() || s.peek() != w) fail(s, "unrecognized value (true/false must be lowercase)");
        s.bump();
    }
    return word == "true";
}

// ---- Datetime -----------------------------------------------------------------

/// Scans the datetime token after the look-ahead hit; raw keeps the whole
/// span as written (stored verbatim in the datetime value).
inline cpptoml::datetime scan_datetime(scanner& s) {
    const std::size_t begin_line = s.line;
    const std::size_t begin_col = s.col;
    const rfc::scan_out r = rfc::scan(s);
    if (!r.ok) {
        fail_at(begin_line, begin_col,
                std::string("invalid datetime: ") + r.reason);
    }
    return cpptoml::datetime{r.raw};
}

// ---- Integers (radix prefixes) --------------------------------------------------

// ---- Floats ---------------------------------------------------------------------

namespace {

[[noreturn]] void not_float(scanner& s, const char* reason) {
    fail(s, std::string("not a valid float: ") + reason);
}

/// Inside a number, both sides of an underscore must be digits of the
/// current class (the cursor sits on the underscore right now).
inline void check_underscore_neighbors(const scanner& s, char prev, char next, bool (*ok)(char)) {
    if (!ok(prev) || !ok(next)) fail(s, "an underscore must sit between digits on both sides");
}

bool dec_ok(char c) { return c >= '0' && c <= '9'; }

}  // namespace

// ---- Main body ---------------------------------------------------------------------

/// Scans a numeric token: integer / float / inf / nan (with an optional
/// +/- prefix).
/// The caller guarantees the cursor is on a digit, '+' or '-', and that the
/// datetime look-ahead did not hit (looks_like_datetime ruled that out).
inline cpptoml::value scan_number(scanner& s) {
    const std::size_t begin_line = s.line, begin_col = s.col;
    bool negative = false;
    bool has_sign = false;  // radix integers take no sign at all (not even +)
    if (s.peek() == '+' || s.peek() == '-') {
        negative = s.peek() == '-';
        has_sign = true;
        s.bump();
    }

    // inf / nan
    if (s.peek() == 'i' || s.peek() == 'n') {
        std::string_view word = (s.peek() == 'i') ? "inf" : "nan";
        for (const char w : word) {
            if (s.eof() || s.peek() != w) {
                fail_at(begin_line, begin_col, "not a valid float: inf/nan incomplete");
            }
            s.bump();
        }
        const double v = (word == "inf")
            ? (negative ? -std::numeric_limits<double>::infinity()
                        : std::numeric_limits<double>::infinity())
            : (negative ? -std::numeric_limits<double>::quiet_NaN()
                        : std::numeric_limits<double>::quiet_NaN());
        return v;
    }

    // 0x / 0o / 0b: radix integers never carry a sign
    if (s.peek() == '0' && !s.eof()) {
        const char p = s.peek_at(1);
        if (p == 'x' || p == 'o' || p == 'b') {
            if (has_sign) {
                fail_at(begin_line, begin_col, "radix integers (0x/0o/0b) do not allow a sign");
            }
            const int base = (p == 'x') ? 16 : (p == 'o') ? 8 : 2;
            s.bump_n(2);
            std::uint64_t acc = 0;
            bool any = false;
            for (;;) {
                if (!s.eof() && charset::is_radix_digit(s.peek(), base)) {
                    const unsigned d = charset::radix_value(s.peek(), base);
                    if (acc > (UINT64_MAX - d) / static_cast<unsigned>(base)) {
                        fail_at(begin_line, begin_col, "integer exceeds the 64-bit representable range");
                    }
                    acc = acc * static_cast<unsigned>(base) + d;
                    s.bump();
                    any = true;
                } else if (!s.eof() && s.peek() == '_') {
                    if (!any) fail_at(begin_line, begin_col, "an underscore must sit between digits on both sides");
                    s.bump();
                    if (s.eof() || !charset::is_radix_digit(s.peek(), base)) {
                        fail_at(begin_line, begin_col, "an underscore must sit between digits on both sides");
                    }
                } else {
                    break;
                }
            }
            if (!any) fail_at(begin_line, begin_col, "not a valid integer: no digits after the prefix");
            return static_cast<std::int64_t>(acc);
        }
    }

    // decimal: optional sign + integer part
    if (s.eof() || !dec_ok(s.peek())) {
        fail_at(begin_line, begin_col, negative ? "not a valid number: no digits after the sign"
                                                : "not a valid number");
    }
    std::string digits;
    {
        const char first = s.peek();
        s.bump();
        if (first == '0') {
            if (!s.eof() && (dec_ok(s.peek()) || s.peek() == '_')) {
                fail_at(begin_line, begin_col, "leading zero (a decimal integer cannot start with 0 followed by digits)");
            }
            digits.push_back('0');
        } else {
            digits.push_back(first);
            while (!s.eof()) {
                const char c = s.peek();
                if (dec_ok(c)) { digits.push_back(c); s.bump(); }
                else if (c == '_') {
                    check_underscore_neighbors(s, digits.back(), s.peek_at(1), dec_ok);
                    s.bump();
                } else break;
            }
        }
    }

    // fraction / exponent
    bool is_float = false;
    if (!s.eof() && s.peek() == '.') {
        is_float = true;
        s.bump();
        if (s.eof() || !dec_ok(s.peek())) not_float(s, "a digit is required after the decimal point");
        digits.push_back('.');
        while (!s.eof()) {
            const char c = s.peek();
            if (dec_ok(c)) { digits.push_back(c); s.bump(); }
            else if (c == '_') { check_underscore_neighbors(s, digits.back(), s.peek_at(1), dec_ok); s.bump(); }
            else break;
        }
    }
    if (!s.eof() && (s.peek() == 'e' || s.peek() == 'E')) {
        is_float = true;
        digits.push_back(s.peek());
        s.bump();
        if (!s.eof() && (s.peek() == '+' || s.peek() == '-')) {
            digits.push_back(s.peek());
            s.bump();
        }
        if (s.eof() || !dec_ok(s.peek())) not_float(s, "a digit is required after the exponent");
        while (!s.eof()) {
            const char c = s.peek();
            if (dec_ok(c)) { digits.push_back(c); s.bump(); }
            else if (c == '_') { check_underscore_neighbors(s, digits.back(), s.peek_at(1), dec_ok); s.bump(); }
            else break;
        }
    }

    if (!is_float) {
        // decimal integer: accumulate into uint64 first, then restore the
        // sign and clamp to the int64 range.
        std::uint64_t mag = 0;
        for (const char c : digits) {
            const unsigned d = static_cast<unsigned>(c - '0');
            if (mag > (UINT64_MAX - d) / 10) {
                fail_at(begin_line, begin_col, "integer exceeds the 64-bit representable range");
            }
            mag = mag * 10 + d;
        }
        constexpr std::uint64_t kInt64Max = 9223372036854775807ULL;
        constexpr std::uint64_t kInt64Min = 9223372036854775808ULL;
        if (negative) {
            if (mag > kInt64Min) fail_at(begin_line, begin_col, "integer exceeds the 64-bit representable range");
            if (mag == kInt64Min) return std::numeric_limits<std::int64_t>::min();
            return -static_cast<std::int64_t>(mag);
        }
        if (mag > kInt64Max) fail_at(begin_line, begin_col, "integer exceeds the 64-bit representable range");
        return static_cast<std::int64_t>(mag);
    }
    if (negative) digits.insert(digits.begin(), '-');

    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(digits.c_str(), &end);
    if (end != digits.c_str() + digits.size()) {
        not_float(s, "malformed number shape");
    }
    if (errno == ERANGE) {
        if (std::isinf(v)) fail_at(begin_line, begin_col, "float out of range (exceeds the 64-bit floating-point range)");
        // underflow to 0 is fine (TOML only demands lossless
        // representability; tiny values may degrade to 0)
        return 0.0;
    }
    return v;
}

}  // namespace cpptoml::detail::strnum
