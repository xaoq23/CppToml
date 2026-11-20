// src/main.cpp — tomlcli: parse / validate / selftest CLI for the cpptoml // subset.
#include <cpptoml/format.hpp>
#include <cpptoml/parse.hpp>
#include <cpptoml/value.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "selftest.hpp"

namespace {

constexpr const std::string_view kVersion = "tomlcli 0.2.1";

int usage() {
    std::cout
        << "tomlcli —— TOML Parser Written in C++ 23\n"
        << "Usage:\n"
        << "  tomlcli <file.toml>        Parse and print the value tree\n"
        << "  tomlcli check <file.toml>  Only validate (report errors with line:column)\n"
        << "  tomlcli version            Print version\n"
        << "  tomlcli selftest           Run built-in tests\n";
    return 2;
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

int report(const std::string& path, bool quiet) {
    const auto text = read_file(path);
    if (!text) {
        std::cerr << "Cannot read file: " << path << '\n';
        return 1;
    }
    const auto parsed = cpptoml::parse(*text);
    if (!parsed) {
        std::cout << path << ':' << parsed.error().line << ':' << parsed.error().column
                  << ": Error: " << parsed.error().message << '\n';
        return 1;
    }
    if (quiet) {
        std::cout << path << ": OK (top-level " << parsed->entries.size() << " keys)\n";
    } else {
        std::cout << cpptoml::format_tree(*parsed) << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);
    if (args.empty()) return usage();
    if (args[0] == "version") {
        std::cout << kVersion << '\n';
        return 0;
    }
    if (args[0] == "selftest") return selftest_main();
    if (args[0] == "check" && args.size() == 2) return report(args[1], true);
    if (args.size() == 1 && args[0] != "check") return report(args[0], false);
    return usage();
}
