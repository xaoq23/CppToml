// lib/detail/charset.hpp — explicit ASCII character class tables.
//
// Private header: classification uses ASCII code ranges only
// (locale-sensitive std::isalnum can misread lead bytes of multi-byte
// letters such as 'é', so every range is hand-written here).
#pragma once

#include <cstdint>

namespace cpptoml::detail::charset {

/// Whitespace: space and tab only (newlines are layered separately).
inline bool is_ws(char c) { return c == ' ' || c == '\t'; }

/// 0-9.
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

/// 0-9 a-f A-F (\u/\U hex digits; among radix prefixes a-f only follows
/// 0x).
inline bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline int hex_value(char c) {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

/// Bare key characters: A-Z a-z 0-9 _ - (TOML 1.0 allows ASCII only; any
/// non-ASCII byte is a hard error).
inline bool is_bare_key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/// Digits valid for the radix (uppercase is accepted after 0x: both 0x1F
/// and 0x1f parse).
inline bool is_radix_digit(char c, int base) {
    switch (base) {
        case 16: return is_hex_digit(c);
        case 8:  return c >= '0' && c <= '7';
        case 2:  return c == '0' || c == '1';
        default: return is_digit(c);
    }
}

/// Radix digit to value (hex maps a-f; octal/binary use 0-7).
inline unsigned radix_value(char c, int base) {
    if (base == 16) return static_cast<unsigned>(hex_value(c));
    return static_cast<unsigned>(c - '0');
}

}  // namespace cpptoml::detail::charset
