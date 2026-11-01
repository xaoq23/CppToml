// lib/detail/rfc3339.hpp — syntax and range scan of TOML 1.0 datetime
// tokens.
//
// One cursor pass plus a concrete failure reason; throws nothing and builds
// no messages (parser.cpp composes them). Grammar = full-date [Tt/SP time]
// or a bare time:
//   * full-date: 4-digit year, month 01-12, day 01..end of that month
//     (February per leap year);
//   * time: exact two-digit widths, hour 00-23, minute 00-59, second 00-60
//     (leap second); the second is mandatory (plain 17:45 and T17:45 are
//     both rejected); after '.' at least one digit is required (no comma
//     fractions);
//   * offset: Z|z or ±HH:MM (hour 00-24, minute 00-59);
//   * local time and local date never take an offset;
//   * the single-space separator only counts when a digit follows the
//     space, otherwise it is trailing whitespace.
#pragma once

#include "scanner.hpp"

#include <string>

namespace cpptoml::detail::rfc {

/// Scan result: ok = success (s advanced, raw holds the whole token); on
/// failure reason tells why.
struct scan_out {
    bool ok = false;
    std::string raw;
    const char* reason = nullptr;
};

/// Read-only look-ahead: does the current word start like a date (yyyy-)
/// or a time (hh:)?
/// Once the look-ahead hits, the value commits to the datetime path: any
/// later failure is an error, never a fallback to number scanning.
inline bool looks_like_datetime(const scanner& s) {
    const auto digit_at = [&](std::size_t i) {
        const char c = s.peek_at(i);
        return c >= '0' && c <= '9';
    };
    if (digit_at(0) && digit_at(1) && digit_at(2) && digit_at(3) && s.peek_at(4) == '-') {
        return true;
    }
    return digit_at(0) && digit_at(1) && s.peek_at(2) == ':';
}

namespace {

inline bool is_leap_year(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int days_in_month(int y, int m) {
    static constexpr int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap_year(y)) return 29;
    return kDays[m];
}

struct pieces {
    bool ok = false;
    const char* reason = nullptr;
};

/// Two digits; leaves the cursor untouched on failure.
inline bool take2(scanner& s, int& out) {
    const char a = s.peek();
    const char b = s.peek_at(1);
    if (a < '0' || a > '9' || b < '0' || b > '9') return false;
    out = (a - '0') * 10 + (b - '0');
    s.bump();
    s.bump();
    return true;
}

/// partial-time: hh:mm:ss[.frac] (on success the cursor sits past both).
inline pieces take_time(scanner& s) {
    pieces p;
    int hour = 0, minute = 0, second = 0;
    if (!take2(s, hour)) return {false, "hour must be two digits"};
    if (s.peek() != ':') return {false, "':' missing after the hour"};
    s.bump();
    if (!take2(s, minute)) return {false, "minute must be two digits"};
    if (minute > 59) return {false, "minute out of the 00-59 range"};
    if (s.peek() != ':') return {false, "':' missing after the minute (seconds are not optional)"};
    s.bump();
    if (!take2(s, second)) return {false, "second must be two digits"};
    if (hour > 23) return {false, "hour out of the 00-23 range"};
    if (second > 60) return {false, "second out of the 00-60 range (leap second allowed)"};
    // Optional fraction: '.' must be followed by >= 1 digit (time-secfloat)
    if (s.peek() == '.') {
        s.bump();
        std::size_t frac = 0;
        while (!s.eof() && s.peek() >= '0' && s.peek() <= '9') {
            s.bump();
            ++frac;
        }
        if (frac == 0) return {false, "a digit is required after the decimal point"};
    }
    p.ok = true;
    return p;
}

/// Z|z or ±HH:MM; a missing tail or a one-digit minute is rejected.
inline pieces take_offset(scanner& s) {
    pieces p;
    if (const char z = s.peek(); z == 'Z' || z == 'z') {
        s.bump();
        p.ok = true;
        return p;
    }
    if (s.peek() != '+' && s.peek() != '-') return {false, "unexpected content after the time"};
    s.bump();
    int oh = 0, om = 0;
    if (!take2(s, oh)) return {false, "the offset hour must be two digits"};
    if (s.peek() != ':') return {false, "':' missing after the offset hour"};
    s.bump();
    if (!take2(s, om)) return {false, "the offset minute must be two digits"};
    if (oh > 24) return {false, "the offset hour is out of the 00-24 range"};
    if (om > 59) return {false, "the offset minute is out of the 00-59 range"};
    p.ok = true;
    return p;
}

}  // namespace

/// Scan one datetime token; looks_like_datetime must have hit before calling.
inline scan_out scan(scanner& s) {
    scan_out out;
    const std::size_t begin = s.pos;
    const auto fail_at = [&](const char* why) {
        out.ok = false;
        out.reason = why;
        out.raw = s.src.substr(begin, s.pos - begin);
        return out;
    };

    // Branch 1: bare time (hh:...) — no offset may follow
    if (const auto digit_at = [&](std::size_t i) {
            const char c = s.peek_at(i);
            return c >= '0' && c <= '9';
        };
        digit_at(0) && digit_at(1) && s.peek_at(2) == ':') {
        if (const pieces p = take_time(s); !p.ok) return fail_at(p.reason);
        if (const char z = s.peek(); z == 'Z' || z == 'z' || z == '+' || z == '-') {
            return fail_at("a local time cannot carry a timezone offset");
        }
        out.ok = true;
        out.raw = s.src.substr(begin, s.pos - begin);
        return out;
    }

    // Branch 2: full-date ...
    int year = 0, month = 0, day = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = s.peek();
        if (c < '0' || c > '9') return fail_at("the year is not 4 digits");
        year = year * 10 + (c - '0');
        s.bump();
    }
    if (s.peek() != '-') return fail_at("'-' missing after the year");
    s.bump();
    if (!take2(s, month)) return fail_at("the month must be two digits");
    if (month == 0) return fail_at("the month cannot be 00");
    if (month > 12) return fail_at("the month is out of the 01-12 range");
    if (s.peek() != '-') return fail_at("'-' missing after the month");
    s.bump();
    if (!take2(s, day)) return fail_at("the day must be two digits");
    if (day == 0) return fail_at("the day cannot be 00");
    if (day > days_in_month(year, month)) return fail_at("the day exceeds the month's maximum day count (leap-year rules included)");

    // After the date: a local date stops here, or continue as a full datetime
    const char sep = s.peek();
    const auto digit_c = [](char c) { return c >= '0' && c <= '9'; };
    const bool time_follows = sep == 'T' || sep == 't' ||
                              (sep == ' ' && digit_c(s.peek_at(1)));
    if (!time_follows) {
        out.ok = true;  // local date; trailing space/comment is handled by
                        // expect_line_end
        out.raw = s.src.substr(begin, s.pos - begin);
        return out;
    }
    s.bump();  // T / t / single space
    if (const pieces p = take_time(s); !p.ok) return fail_at(p.reason);
    if (const char z = s.peek(); z == 'Z' || z == 'z' || z == '+' || z == '-') {
        if (const pieces p = take_offset(s); !p.ok) return fail_at(p.reason);
    }
    out.ok = true;
    out.raw = s.src.substr(begin, s.pos - begin);
    return out;
}

}  // namespace cpptoml::detail::rfc
