// cpptoml/value.hpp — TOML value model.
//
// Public API. Tables and arrays are held indirectly via shared_ptr so the
// value type can recurse; datetimes keep the raw token (no timezone math
// in this project's scope, see README).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cpptoml {

struct table;
struct array;

/// TOML date / datetime / local time; keeps the raw token as written.
struct datetime {
    std::string raw;
};

/// TOML value: string / integer / float / bool / datetime / array / table.
using value = std::variant<
    std::string,
    std::int64_t,
    double,
    bool,
    datetime,
    std::shared_ptr<table>,
    std::shared_ptr<array>>;

struct array {
    std::vector<value> items;
};

struct table {
    std::unordered_map<std::string, value> entries;
};

// ---- Convenience readers: nullptr when the type does not match --------------

inline const std::string* get_string(const value& v) {
    return std::get_if<std::string>(&v);
}

inline const std::int64_t* get_int(const value& v) {
    return std::get_if<std::int64_t>(&v);
}

inline const double* get_float(const value& v) {
    return std::get_if<double>(&v);
}

inline const bool* get_bool(const value& v) {
    return std::get_if<bool>(&v);
}

inline const datetime* get_datetime(const value& v) {
    return std::get_if<datetime>(&v);
}

inline const table* get_table(const value& v) {
    const auto* p = std::get_if<std::shared_ptr<table>>(&v);
    return p ? p->get() : nullptr;
}

inline const array* get_array(const value& v) {
    const auto* p = std::get_if<std::shared_ptr<array>>(&v);
    return p ? p->get() : nullptr;
}

// ---- Generic accessors (toml++ style convenience entry points) --------------

/// True for every type a TOML value can hold (tables and arrays use their
/// pointer forms, as stored inside the variant).
template <typename T>
inline constexpr bool is_value_type_v = std::is_same_v<T, std::string> ||
    std::is_same_v<T, std::int64_t> || std::is_same_v<T, double> ||
    std::is_same_v<T, bool> || std::is_same_v<T, datetime> ||
    std::is_same_v<T, table> || std::is_same_v<T, array>;

/// Read a value by type: returns a pointer on success, nullptr when `v` does
/// not hold T (mirrors std::get_if). T may also be table or array; the
/// variant's shared_ptr holder is unwrapped, so `get<table>(v)` mirrors
/// `get_table(v)` and the result is borrowed (never owns the node).
template <typename T>
const T* get(const value& v) {
    static_assert(is_value_type_v<T>,
                  "get<T>: T must be one of the seven TOML value types");
    if constexpr (std::is_same_v<T, table> || std::is_same_v<T, array>) {
        const auto* p = std::get_if<std::shared_ptr<T>>(&v);
        return p ? p->get() : nullptr;
    } else {
        return std::get_if<T>(&v);
    }
}

/// Apply a visitor to a value: a thin forwarding wrapper over std::visit.
/// Overload set of the visitor must cover std::string, std::int64_t, double,
/// bool, datetime, std::shared_ptr<table> and std::shared_ptr<array>.
template <typename Visitor>
decltype(auto) visit(Visitor&& vis, const value& v) {
    return std::visit(std::forward<Visitor>(vis), v);
}

}  // namespace cpptoml
