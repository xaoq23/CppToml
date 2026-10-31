// lib/value.cpp — single-value repr.
//
// Used by format_tree and the CLI: deterministic, readable, unambiguous
// (strings escaped, keys sorted).

#include <cpptoml/format.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <string>

namespace cpptoml {
namespace {

std::string escape_repr(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\f': out += "\\u000C"; break;
            case '\b': out += "\\u0008"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04X}", static_cast<unsigned>(c));
                } else {
                    out.push_back(c);
                }
        }
    }
    out += '"';
    return out;
}

}  // namespace

std::string format_value(const value& v) {
    if (const auto* s = get_string(v)) return escape_repr(*s);
    if (const auto* n = get_int(v)) return std::format("{}", *n);
    if (const auto* f = get_float(v)) {
        if (std::isnan(*f)) return std::signbit(*f) ? "-nan" : "nan";
        if (std::isinf(*f)) return std::signbit(*f) ? "-inf" : "inf";
        return std::format("{}", *f);
    }
    if (const auto* b = get_bool(v)) return *b ? "true" : "false";
    if (const auto* d = get_datetime(v)) return d->raw;
    if (const auto* t = get_table(v)) {
        std::map<std::string, const value*, std::less<>> ordered;
        for (const auto& [k, item] : t->entries) ordered.emplace(k, &item);
        std::string out = "{";
        bool first = true;
        for (const auto& [k, item] : ordered) {
            if (!first) out += ", ";
            out += k + " = " + format_value(*item);
            first = false;
        }
        out += '}';
        return out;
    }
    if (const auto* a = get_array(v)) {
        std::string out = "[";
        bool first = true;
        for (const auto& item : a->items) {
            if (!first) out += ", ";
            out += format_value(item);
            first = false;
        }
        out += ']';
        return out;
    }
    return "<invalid>";
}

}  // namespace cpptoml
