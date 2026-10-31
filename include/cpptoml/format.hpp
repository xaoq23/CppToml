// cpptoml/format.hpp — diagnostic text representation of values and tables.
//
// Public API: for the CLI display and the selftest; not a TOML serializer.
#pragma once

#include <string>

#include <cpptoml/value.hpp>

namespace cpptoml {

/// Repr of a single value: strings escaped and quoted, arrays / inline
/// tables expanded, keys in lexicographic order.
std::string format_value(const value& v);

/// Render a table as an indented tree; nested tables expand with
/// indentation, keys are ordered lexicographically, and the output is
/// deterministic.
std::string format_tree(const table& t);

}  // namespace cpptoml
