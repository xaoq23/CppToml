// cpptoml/parse.hpp — parsing entry points.
//
// Public API: parse a whole input in one shot; failures carry a line/column.

#pragma once

#include <expected>
#include <string>
#include <string_view>

#include <cpptoml/value.hpp>

namespace cpptoml {

/// Location and reason of a parse failure. line/column are 1-based; zero means
/// a document-level error (UTF-8 / control bytes caught by the up-front scan).
struct parse_error {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
};

/// Parse a TOML document (TOML 1.0 scope; see the project README for details).
/// Duplicate keys and table redefinitions are rejected at parse time.
std::expected<table, parse_error> parse(std::string_view source);

/// Read a TOML file (binary mode; BOM handled; UTF-8 verified) and parse it.
/// On an unreadable file the error uses line = column = 0.
std::expected<table, parse_error> parse_file(std::string_view path);

}  // namespace cpptoml
