// examples/print_tree.cpp — parse a TOML file and render its value tree.
//
// Demonstrates cpptoml::parse_file (the file entry point), the error
// contract (1-based line/column, no exceptions), a full cpptoml::visit walk
// over every variant arm, and the read-only format_tree renderer.
//
// Usage: print_tree <file.toml>

#include <cpptoml/format.hpp>
#include <cpptoml/parse.hpp>
#include <cpptoml/value.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace {

// Single callable over every variant arm (the canonical overloaded pattern);
// cpptoml::visit forwards to std::visit, which takes exactly one callable.
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// Depth-first walk; the visitor records one entry per value encountered.
struct census {
    int strings    = 0;
    int ints       = 0;
    int floats     = 0;
    int bools      = 0;
    int datetimes  = 0;
    int tables     = 0;
    int arrays     = 0;

    void walk_value(const cpptoml::value& v);
    void walk_table(const cpptoml::table& t) {
        ++tables;
        for (const auto& kv : t.entries) walk_value(kv.second);
    }
    void walk_array(const cpptoml::array& a) {
        ++arrays;
        for (const cpptoml::value& v : a.items) walk_value(v);
    }
};

void census::walk_value(const cpptoml::value& v) {
    cpptoml::visit(
        overloaded{
            [this](const std::string&) { ++strings; },
            [this](std::int64_t) { ++ints; },
            [this](double) { ++floats; },
            [this](bool) { ++bools; },
            [this](const cpptoml::datetime&) { ++datetimes; },
            [this](const std::shared_ptr<cpptoml::table>& t) { walk_table(*t); },
            [this](const std::shared_ptr<cpptoml::array>& a) { walk_array(*a); },
        },
        v);
}

int fail(const std::string& what) {
    std::cerr << what << '\n';
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return fail("usage: print_tree <file.toml>");

    // File entry point: binary read, BOM handled, UTF-8 verified; an
    // unreadable file reports line = column = 0 without touching exceptions.
    const auto parsed = cpptoml::parse_file(argv[1]);
    if (!parsed) {
        const cpptoml::parse_error& e = parsed.error();
        std::cerr << argv[1] << ':' << e.line << ':' << e.column << ' '
                  << e.message << '\n';
        return 1;
    }

    std::cout << cpptoml::format_tree(*parsed) << '\n';

    census c;
    c.walk_table(*parsed);
    std::cout << "records: " << c.strings << " string(s), " << c.ints
              << " integer(s), " << c.floats << " float(s), " << c.bools
              << " boolean(s), " << c.datetimes << " datetime(s), " << c.tables
              << " table(s), " << c.arrays << " array(s)\n";
    return 0;
}
