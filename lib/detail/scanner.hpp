// lib/detail/scanner.hpp — lexical cursor: line/column tracking and
// single-character helpers.
//
// Private header: shared by parser.cpp and the other detail headers, never
// exposed in include/. Parse failures propagate by throwing parse_abort,
// which parse() catches at the top and converts into std::expected.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace cpptoml::detail {

struct scanner;

/// Parse exception: carries the offending position and message; parse()
/// catches it and packs it into parse_error.
struct parse_abort {
    std::size_t line = 0;
    std::size_t col = 0;
    std::string msg;
};

/// Throw at the scanner's current position.
[[noreturn]] void fail(const scanner& s, std::string msg);

/// Throw with a precomputed position (pre-scan / document-level errors).
[[noreturn]] void fail_at(std::size_t line, std::size_t col, std::string msg);

struct scanner {
    std::string_view src;
    std::size_t pos = 0;
    std::size_t line = 1;
    std::size_t col = 1;

    explicit scanner(std::string_view text) : src(text) {}

    bool eof() const { return pos >= src.size(); }
    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char peek_at(std::size_t ahead) const {
        return pos + ahead < src.size() ? src[pos + ahead] : '\0';
    }

    void bump() {
        if (eof()) return;
        const char c = src[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else if (c != '\r') {
            ++col;
        }
    }

    void bump_n(std::size_t n) {
        while (n-- > 0) bump();
    }

    /// Longest run of the same character from the current position.
    std::size_t run_length(char c) const {
        std::size_t n = 0;
        while (pos + n < src.size() && src[pos + n] == c) ++n;
        return n;
    }

    /// Newline: LF, or a properly paired CRLF (pre-scan rules out lone CR).
    bool at_newline() const {
        return peek() == '\n' || (peek() == '\r' && peek_at(1) == '\n');
    }

    void bump_newline() {
        if (peek() == '\n') { bump(); return; }  // LF
        bump();                                  // CR
        if (peek() == '\n') bump();              // CRLF finishes with LF
    }

    void skip_spaces() {
        while (!eof() && (peek() == ' ' || peek() == '\t')) bump();
    }
};

inline void fail(const scanner& s, std::string msg) {
    throw parse_abort{s.line, s.col, std::move(msg)};
}

inline void fail_at(std::size_t line, std::size_t col, std::string msg) {
    throw parse_abort{line, col, std::move(msg)};
}

/// Consume a comment up to (not including) the newline; no-op if absent.
inline void skip_comment(scanner& s) {
    while (!s.eof() && s.peek() != '\n') s.bump();
}

/// Skip any mix of spaces, comments and newlines (arrays only; entries may
/// be interleaved freely).
inline void skip_ws_comments_lines(scanner& s) {
    for (;;) {
        s.skip_spaces();
        if (s.eof()) return;
        if (s.peek() == '#') { skip_comment(s); continue; }
        if (s.at_newline()) { s.bump_newline(); continue; }
        return;
    }
}

/// Byte offset → (line, column); matches scanner counting (LF breaks the
/// line, CR never counts as a column).
inline std::pair<std::size_t, std::size_t> line_col_of(std::string_view src, std::size_t off) {
    std::size_t line = 1, col = 1;
    for (std::size_t i = 0; i < off && i < src.size(); ++i) {
        const char c = src[i];
        if (c == '\n') { ++line; col = 1; }
        else if (c != '\r') ++col;
    }
    return {line, col};
}

}  // namespace cpptoml::detail
