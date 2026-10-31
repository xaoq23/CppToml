// lib/format.cpp — indented tree rendering for tables (read-only display;
// not a TOML serializer).

#include <cpptoml/format.hpp>

#include <format>
#include <map>
#include <string>
#include <utility>

namespace cpptoml {
namespace {

std::string indent(std::size_t depth) {
    return std::string(depth * 2, ' ');
}

void append_table(std::string& out, const table& t, std::size_t depth) {
    std::map<std::string, const value*, std::less<>> ordered;
    for (const auto& [k, v] : t.entries) ordered.emplace(k, &v);
    for (const auto& [k, v] : ordered) {
        if (const table* sub = get_table(*v)) {
            out += indent(depth) + k + ":\n";
            append_table(out, *sub, depth + 1);
        } else {
            out += indent(depth) + k + " = " + format_value(*v) + '\n';
        }
    }
}

}  // namespace

std::string format_tree(const table& t) {
    std::string out = std::format("<table> 顶层 {} 个键\n", t.entries.size());
    append_table(out, t, 1);
    return out;
}

}  // namespace cpptoml
