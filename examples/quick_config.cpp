// examples/quick_config.cpp — the five-minute tour of cpptoml.
//
// A self-contained sample: describes an embedded configuration, parses it
// with one call, reads fields back with the typed getters, and shows the
// error contract for ill-formed input.
//
// Build (standalone): g++ -std=c++23 -Iinclude -o quick_config
//     examples/quick_config.cpp lib/parser.cpp lib/value.cpp lib/format.cpp

#include <cpptoml/format.hpp>
#include <cpptoml/parse.hpp>
#include <cpptoml/value.hpp>

#include <cstdint>
#include <iostream>
#include <string>

int main() {
    // What a typical application config looks like. Comments and blank lines
    // are allowed; datetime values are stored verbatim (raw token).
    const std::string config = R"TOML(
# quick_config
title = "demo"

[owner]
name  = "Alice"
email = "alice@example.com"

[db]
ports = [8001, 8001, 8002]
enabled = true

[[servers]]
host = "alpha"
ip   = "10.0.0.1"

[[servers]]
host = "beta"
ip   = "10.0.0.2"
)TOML";

    // All-in-one parse; failures carry a 1-based line/column and a reason.
    const auto parsed = cpptoml::parse(config);
    if (!parsed) {
        const cpptoml::parse_error& e = parsed.error();
        std::cerr << e.line << ':' << e.column << ' ' << e.message << '\n';
        return 1;
    }
    const cpptoml::table& root = *parsed;

    // Typed getters return a pointer on success, nullptr on a type mismatch.
    const std::string* title = cpptoml::get_string(root.entries.at("title"));
    std::cout << "title   = " << (title ? *title : "(none)") << '\n';

    // The [db] header is a sub-table; dig into it, then read its leaves.
    const cpptoml::table& db = *cpptoml::get_table(root.entries.at("db"));
    const bool* enabled = cpptoml::get_bool(db.entries.at("enabled"));
    const cpptoml::array* ports = cpptoml::get_array(db.entries.at("ports"));
    std::cout << "enabled = " << (enabled ? "yes" : "no") << '\n'
              << "ports   = " << (ports ? ports->items.size() : 0) << '\n';

    // Array-of-tables ([[servers]]) elements are ordinary tables.
    for (const cpptoml::value& v : cpptoml::get_array(root.entries.at("servers"))->items) {
        const cpptoml::table& s = *cpptoml::get_table(v);
        std::cout << "server  = "
                  << *cpptoml::get_string(s.entries.at("host")) << '\n';
    }

    // The whole tree, read-only; leaves print sorted lexicographically.
    std::cout << '\n' << cpptoml::format_tree(root) << '\n';

    // Datetimes are stored as their raw token and print byte-for-byte.
    const auto bad = cpptoml::parse("dob = 1979-05-27T07:32:00Z\nx = [ 1, 2");
    if (!bad) {  // expected failure: an unterminated array
        const cpptoml::parse_error& e = bad.error();
        std::cout << "dob     = 1979-05-27T07:32:00Z\n"
                  << "ill-formed sample rejected at " << e.line << ':'
                  << e.column << '\n';
    }
    return 0;
}
