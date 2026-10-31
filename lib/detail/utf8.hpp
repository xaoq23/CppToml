// lib/detail/utf8.hpp — whole-input UTF-8 and control-byte pre-scan.
//
// One linear pass over the whole input before any parsing starts:
//   * strict UTF-8: rejects overlong encodings, surrogates, out-of-range
//     code points and truncated sequences;
//   * BOM (U+FEFF) is only legal at the very start (parse() strips one
//     leading BOM; any other one is an error);
//   * control bytes in layers: NUL..BS, VT, FF, 0x0E..0x1F and DEL are
//     rejected everywhere; TAB is always allowed; LF is allowed (inside
//     single-line strings the string scanner rejects it separately); CR
//     must appear paired (CRLF), a lone CR is rejected everywhere.
// The lexical and string paths therefore only have to deal with LF and
// paired CR, without repeating control-byte checks at every site.
#pragma once

#include <string_view>
#include <cstddef>

namespace cpptoml::detail::utf8 {

/// Pre-scan result: passes when ok; otherwise bad holds the offending byte
/// offset (0-based).
struct check_result {
    bool ok;
    std::size_t bad;
};

/// Validate input that has already had its leading BOM stripped.
inline check_result check(std::string_view src) {
    const std::size_t n = src.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        // ---- Control bytes (00..1F and 7F) ----
        if (c <= 0x1F) {
            if (c == 0x09 || c == '\n') { ++i; continue; }  // TAB and LF allowed
            if (c == '\r') {
                if (i + 1 < n && src[i + 1] == '\n') { i += 2; continue; }  // CRLF
                return {false, i};  // lone CR
            }
            return {false, i};  // NUL..BS/VT/FF/0x0E..1F
        }
        if (c == 0x7F) return {false, i};  // DEL: rejected everywhere
        // ---- UTF-8 multi-byte sequences ----
        if (c < 0x80) { ++i; continue; }
        std::size_t len = 0;
        char32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
        else return {false, i};  // C0/C1 (overlong), F5..FF, or a lone
                                 // 0x80..0xBF continuation byte as lead
        if (i + len > n) return {false, i};  // truncated
        // Continuation-byte check + code point assembly
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(src[i + k]);
            if (cc < 0x80 || cc > 0xBF) return {false, i};
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Overlong: the lead-byte ranges above already rule out most of it;
        // enforce each length's lower bound here as well
        if (len == 2 && cp < 0x80) return {false, i};
        if (len == 3 && cp < 0x800) return {false, i};
        if (len == 4 && cp < 0x10000) return {false, i};
        // Surrogates (ED A0..BF sneaks past the lead-byte range; re-check by
        // code point) and out-of-range
        if (cp >= 0xD800 && cp <= 0xDFFF) return {false, i};
        if (cp > 0x10FFFF) return {false, i};
        // U+FEFF may only appear at the very start (parse() strips one
        // leading BOM; anything left is an error)
        if (cp == 0xFEFF) return {false, i};
        i += len;
    }
    return {true, 0};
}

}  // namespace cpptoml::detail::utf8
