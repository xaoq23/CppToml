// src/selftest.cpp — embedded test suite: valid cases verify field by
// field, invalid cases verify the error keywords.
//
// A failed assertion prints the case name and keeps going; the tail sums up.
// The return code feeds CI; normal return codes are 0/1.

#include "selftest.hpp"

#include <cpptoml/format.hpp>
#include <cpptoml/parse.hpp>
#include <cpptoml/value.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using parse_result = std::expected<cpptoml::table, cpptoml::parse_error>;

using cpptoml::value;

const cpptoml::value* pick(const cpptoml::table& t, std::string_view key) {
    const auto it = t.entries.find(std::string(key));
    return it == t.entries.end() ? nullptr : &it->second;
}

bool is_int(const value& v, std::int64_t expect) {
    const auto* n = cpptoml::get_int(v);
    return n != nullptr && *n == expect;
}

bool is_float(const value& v, double expect) {
    const auto* f = cpptoml::get_float(v);
    return f != nullptr && *f == expect;
}

bool is_str(const value& v, std::string_view expect) {
    const auto* s = cpptoml::get_string(v);
    return s != nullptr && *s == expect;
}

bool is_bool(const value& v, bool expect) {
    const auto* b = cpptoml::get_bool(v);
    return b != nullptr && *b == expect;
}

bool is_dt(const value& v, std::string_view raw) {
    const auto* d = cpptoml::get_datetime(v);
    return d != nullptr && d->raw == raw;
}

// ---- Valid cases ---------------------------------------------------------------

struct ok_case {
    const char* name;
    const char* toml;
    std::function<bool(const cpptoml::table&)> verify;
};

// Main corpus: a multi-feature document in official-test style.
const char* MAIN_DOC = R"toml(
# 注释会被忽略
title = "cpp-toml main"
owner.name = "Tom Preston-Werner"
owner.dob  = 1979-05-27T07:32:00Z

db.enabled = true
db.ports = [8001, 8001, 8002]
db.data = [ ["delta", "phi"], [3.14] ]
db.limits.max = 1e4
db.limits.min = -0.2

[servers.alpha]
ip = "10.0.0.1"
role = "frontend"

[servers.beta]
ip = "10.0.0.2"
role = "backend"

[[products]]
name = "Hammer"
sku = 738594937

[[products]]

[[products]]
name = "Nail"
sku = 284758393
kind = "stick"
)toml";

std::vector<ok_case> ok_cases() {
    using namespace cpptoml;
    std::vector<ok_case> cases;
    cases.push_back({"main/title", MAIN_DOC, [](const table& t) {
        return pick(t, "title") != nullptr && is_str(*pick(t, "title"), "cpp-toml main");
    }});
    cases.push_back({"main/dotted-key", MAIN_DOC, [](const table& t) {
        const value* owner = pick(t, "owner");
        if (owner == nullptr) return false;
        const table* ot = get_table(*owner);
        if (ot == nullptr) return false;
        return is_str(*pick(*ot, "name"), "Tom Preston-Werner");
    }});
    cases.push_back({"main/datetime-raw", MAIN_DOC, [](const table& t) {
        const value* owner = pick(t, "owner");
        const table* ot = owner ? get_table(*owner) : nullptr;
        return ot != nullptr && pick(*ot, "dob") != nullptr &&
               is_dt(*pick(*ot, "dob"), "1979-05-27T07:32:00Z");
    }});
    cases.push_back({"main/array-ints", MAIN_DOC, [](const table& t) {
        const value* db = pick(t, "db");
        const table* dt = db ? get_table(*db) : nullptr;
        if (dt == nullptr) return false;
        const value* ports = pick(*dt, "ports");
        const array* pa = ports ? get_array(*ports) : nullptr;
        if (pa == nullptr || pa->items.size() != 3) return false;
        return is_int(pa->items[0], 8001) && is_int(pa->items[1], 8001) &&
               is_int(pa->items[2], 8002);
    }});
    cases.push_back({"main/nested-arrays", MAIN_DOC, [](const table& t) {
        const value* db = pick(t, "db");
        const table* dt = db ? get_table(*db) : nullptr;
        if (dt == nullptr) return false;
        const value* data = pick(*dt, "data");
        const array* pa = data ? get_array(*data) : nullptr;
        if (pa == nullptr || pa->items.size() != 2) return false;
        const array* i0 = get_array(pa->items[0]);
        const array* i1 = get_array(pa->items[1]);
        if (i0 == nullptr || i1 == nullptr || i0->items.size() != 2 || i1->items.size() != 1) {
            return false;
        }
        return is_str(i0->items[0], "delta") && is_str(i0->items[1], "phi") &&
               is_float(i1->items[0], 3.14);
    }});
    cases.push_back({"main/floats", MAIN_DOC, [](const table& t) {
        const value* db = pick(t, "db");
        const table* dt = db ? get_table(*db) : nullptr;
        if (dt == nullptr) return false;
        const value* limits = pick(*dt, "limits");
        const table* lt = limits ? get_table(*limits) : nullptr;
        if (lt == nullptr) return false;
        return is_float(*pick(*lt, "max"), 1e4) && is_float(*pick(*lt, "min"), -0.2);
    }});
    cases.push_back({"main/aot", MAIN_DOC, [](const table& t) {
        const value* products = pick(t, "products");
        const array* pa = products ? get_array(*products) : nullptr;
        if (pa == nullptr || pa->items.size() != 3) return false;
        const table* first = get_table(pa->items[0]);
        const table* second = get_table(pa->items[1]);
        const table* third = get_table(pa->items[2]);
        if (first == nullptr || second == nullptr || third == nullptr) return false;
        return is_str(*pick(*first, "name"), "Hammer") && second->entries.empty() &&
               is_str(*pick(*third, "name"), "Nail") && is_str(*pick(*third, "kind"), "stick");
    }});
    cases.push_back({"strings/escapes",
                     "esc = \"a\\tb\\nc\\u00e9\"\n",
                     [](const table& t) {
                         const value* v = pick(t, "esc");
                         return v != nullptr && is_str(*v, "a\tb\nc\xC3\xA9");
                     }});
    cases.push_back({"ints/all-radix",
                     "a = +99\nb = -17\nc = 0xdead_bEEF\nd = 0o755\ne = 0b1101\nf = 1_000_000\n",
                     [](const table& t) {
                         return is_int(*pick(t, "a"), 99) && is_int(*pick(t, "b"), -17) &&
                                is_int(*pick(t, "c"), 0xDEADBEEF) &&
                                is_int(*pick(t, "d"), 493) && is_int(*pick(t, "e"), 13) &&
                                is_int(*pick(t, "f"), 1000000);
                     }});
    cases.push_back({"floats/specials",
                     "a = 3.14\nb = 5e22\nc = -0.01\nx = inf\ny = -inf\nz = nan\n",
                     [](const table& t) {
                         const auto* inf = get_float(*pick(t, "x"));
                         const auto* ninf = get_float(*pick(t, "y"));
                         const auto* nan = get_float(*pick(t, "z"));
                         return is_float(*pick(t, "a"), 3.14) && is_float(*pick(t, "b"), 5e22) &&
                                is_float(*pick(t, "c"), -0.01) && inf != nullptr &&
                                std::isinf(*inf) && ninf != nullptr && std::isinf(*ninf) &&
                                *ninf < 0 && nan != nullptr && std::isnan(*nan);
                     }});
    cases.push_back({"strings/literal",
                     "path = 'C:\\Users\\nodejs\\templates'\nempty = ''\n",
                     [](const table& t) {
                         return is_str(*pick(t, "path"), "C:\\Users\\nodejs\\templates") &&
                                is_str(*pick(t, "empty"), "");
                     }});
    cases.push_back({"strings/multiline-basic",
                     "str1 = \"\"\"\nRoses are red\nViolets are blue\"\"\"\nstr2 = \"\"\"\nRoses are red,\nViolets are blue.\n\"\"\"\n",
                     [](const table& t) {
                         return is_str(*pick(t, "str1"), "Roses are red\nViolets are blue") &&
                                is_str(*pick(t, "str2"), "Roses are red,\nViolets are blue.\n");
                     }});
    cases.push_back({"strings/line-ending-backslash",
                     "str3 = \"\"\"\\\n  The quick brown \\\n  fox jumps over the lazy dog.\\\n  \"\"\"\n",
                     [](const table& t) {
                         return is_str(*pick(t, "str3"),
                                       "The quick brown fox jumps over the lazy dog.");
                     }});
    cases.push_back({"strings/multiline-literal",
                     "re = '''I [dw]on't need \\d{2} hanls'''\nm = '''\nraw\ntext'''\n",
                     [](const table& t) {
                         return is_str(*pick(t, "re"), "I [dw]on't need \\d{2} hanls") &&
                                is_str(*pick(t, "m"), "raw\ntext");
                     }});
    cases.push_back({"inline-tables",
                     "point = { x = 1, y = 2 }\nnested = { a.b = 3, empty = {} }\n",
                     [](const table& t) {
                         const value* point = pick(t, "point");
                         const table* pt = point ? get_table(*point) : nullptr;
                         const value* nested = pick(t, "nested");
                         const table* nt = nested ? get_table(*nested) : nullptr;
                         if (pt == nullptr || nt == nullptr) return false;
                         const value* ab = pick(*nt, "a");
                         const table* abt = ab ? get_table(*ab) : nullptr;
                         return is_int(*pick(*pt, "x"), 1) && is_int(*pick(*pt, "y"), 2) &&
                                abt != nullptr && is_int(*pick(*abt, "b"), 3) &&
                                nt->entries.contains("empty");
                     }});
    cases.push_back({"keys/quoted-and-empty",
                     "\"\" = \"blank\"\n'literal-key' = 1\n\"quoted key\".sub = 2\n",
                     [](const table& t) {
                         const value* qk = pick(t, "quoted key");
                         const table* qt = qk ? get_table(*qk) : nullptr;
                         return qt != nullptr && is_int(*pick(*qt, "sub"), 2) &&
                                is_str(*pick(t, ""), "blank") && is_int(*pick(t, "literal-key"), 1);
                     }});
    cases.push_back({"header/implicit-parents",
                     "[a.b.c]\nx = 1\n[a.b]\ny = 2\n[a]\nz = 3\n",
                     [](const table& t) {
                         const value* va = pick(t, "a");
                         const table* ta = va ? get_table(*va) : nullptr;
                         if (ta == nullptr) return false;
                         const value* vb = pick(*ta, "b");
                         const table* tb = vb ? get_table(*vb) : nullptr;
                         if (tb == nullptr) return false;
                         const value* vc = pick(*tb, "c");
                         const table* tc = vc ? get_table(*vc) : nullptr;
                         return tc != nullptr && is_int(*pick(*tc, "x"), 1) &&
                                is_int(*pick(*tb, "y"), 2) && is_int(*pick(*ta, "z"), 3);
                     }});
    cases.push_back({"header/aot-subtable",
                     "[[a]]\nx = 1\n\n[a.sub]\ny = 2\n\n[[a]]\nx = 9\n",
                     [](const table& t) {
                         const value* va = pick(t, "a");
                         const array* aa = va ? get_array(*va) : nullptr;
                         if (aa == nullptr || aa->items.size() != 2) return false;
                         const table* e0 = get_table(aa->items[0]);
                         const table* e1 = get_table(aa->items[1]);
                         if (e0 == nullptr || e1 == nullptr) return false;
                         const value* sub = pick(*e0, "sub");
                         const table* st = sub ? get_table(*sub) : nullptr;
                         return st != nullptr && is_int(*pick(*st, "y"), 2) &&
                                e1->entries.contains("x");
                     }});
    cases.push_back({"bools", "b1 = true\nb2 = false\n", [](const cpptoml::table& t) {
                         return is_bool(*pick(t, "b1"), true) && is_bool(*pick(t, "b2"), false);
                     }});
    cases.push_back({"doc/comments-only",
                     "# 只有注释\n\n   # 缩进注释\n",
                     [](const cpptoml::table& t) { return t.entries.empty(); }});
    cases.push_back({"doc/crlf",
                     "a = 1\r\nb = 'two'\r\n",
                     [](const table& t) {
                         return is_int(*pick(t, "a"), 1) && is_str(*pick(t, "b"), "two");
                     }});
    cases.push_back({"key/empty-quoted-root", "'' = 5\n", [](const table& t) {
                         return is_int(*pick(t, ""), 5);
                     }});
    cases.push_back({"key/empty-quoted-dotted", "''.'' = 1\nx.'' = 2\n", [](const table& t) {
                         const value* e = pick(t, "");
                         const table* et = e ? get_table(*e) : nullptr;
                         const value* x = pick(t, "x");
                         const table* xt = x ? get_table(*x) : nullptr;
                         return et != nullptr && xt != nullptr &&
                                is_int(*pick(*et, ""), 1) && is_int(*pick(*xt, ""), 2);
                     }});
    cases.push_back({"table/empty-name", "['']\nx = 1\n", [](const table& t) {
                         const value* e = pick(t, "");
                         const table* et = e ? get_table(*e) : nullptr;
                         return et != nullptr && is_int(*pick(*et, "x"), 1);
                     }});
    cases.push_back({"ints/signed-zero", "a = -0\nb = +0\n", [](const table& t) {
                         return is_int(*pick(t, "a"), 0) && is_int(*pick(t, "b"), 0);
                     }});
    cases.push_back({"floats/exp-leading-zero", "x = 1e06\n", [](const table& t) {
                         return is_float(*pick(t, "x"), 1e6);
                     }});
    cases.push_back({"floats/underflow-to-zero", "a = 1e-999\n", [](const table& t) {
                         return is_float(*pick(t, "a"), 0.0);
                     }});
    cases.push_back({"aot/dotted-in-second", "[[a]]\nx = 1\n[[a]]\nb.c = 2\n", [](const table& t) {
                         const value* va = pick(t, "a");
                         const array* pa = va ? get_array(*va) : nullptr;
                         if (pa == nullptr || pa->items.size() != 2) return false;
                         const table* e1 = get_table(pa->items[1]);
                         if (e1 == nullptr) return false;
                         const value* vb = pick(*e1, "b");
                         const table* bt = vb ? get_table(*vb) : nullptr;
                         return bt != nullptr && is_int(*pick(*bt, "c"), 2);
                     }});
    return cases;
}

// ---- Invalid cases ---------------------------------------------------------------

struct err_case {
    const char* name;
    const char* toml;
    const char* err_contains;
};

std::vector<err_case> err_cases() {
    return {
        {"dup/key", "k = 1\nk = 2\n", "重复的键"},
        {"dup/dotted", "a.b = 1\na.b = 2\n", "重复的键"},
        {"dup/table-header", "[a]\n[a]\n", "重复定义"},
        {"dup/table-after-dotted", "a.b = 1\n[a]\n", "键值对定义"},
        {"dup/redefine-inline", "a = {}\n[a]\nx = 1\n", "键值对定义"},
        {"dup/header-vs-dotted", "[a]\n[a.b]\n\n[a.c]\nx = 1\n\n[c]\n[a.c]\n", "重复定义"},
        {"num/leading-zero", "x = 01\n", "前导零"},
        {"num/empty-int", "x = 0x\n", "不是合法的整数"},
        {"num/bad-underscore", "x = 1__0\n", "下划线"},
        {"num/trailing-underscore", "x = 1_\n", "下划线"},
        {"num/int-overflow", "x = 99999999999999999999\n", "64 位"},
        {"num/float-overflow", "x = 1e999\n", "超出范围"},
        {"num/bad-float", "x = 1.\n", "不是合法的浮点数"},
        {"num/orphan-sign", "x = -\n", "不是合法的数字"},
        {"str/bad-escape", "s = \"\\q\"\n", "非法转义"},
        {"str/unclosed", "s = \"abc\n", "未闭合"},
        {"str/unclosed-literal", "s = 'abc\n", "未闭合"},
        {"str/bad-codepoint", "s = \"\\uD800\"\n", "代理区"},
        {"str/short-codepoint", "s = \"\\u12\"\n", "十六进制"},
        {"syn/missing-value", "x = \n", "缺少值"},
        {"syn/missing-eq", "x 1\n", "'='"},
        {"syn/trailing-garbage", "x = 1 y = 2\n", "多余内容"},
        {"syn/bare-nonascii-key", "å = 1\n", "键名"},
        {"inline/trailing-comma", "t = { x = 1, }\n", "尾逗号"},
        {"inline/multiline", "t = {\n  x = 1\n}\n", "一行内"},
        {"inline/missing-brace", "t = { x = 1\n", "一行内"},
        {"array/unclosed", "x = [1, 2,\n", "未闭合"},
        {"array/missing-comma", "x = [1 2]\n", "',' 或 ']'"},
        {"header/unclosed", "[a\n", "表头需要"},
        {"header/double-close", "[a]]\n", "多余内容"},
        {"header/empty-part", "[a..b]\n", "键名"},
        {"aot/conflict", "[a]\n[[a]]\n", "数组表"},
        {"num/plus-radix", "x = +0xff\n", "进制整数"},
        {"num/minus-radix", "x = -0b1\n", "进制整数"},
        {"num/us-in-radix", "x = 0x_1\n", "下划线"},
        {"num/zero-underscore", "x = 0_1\n", "前导零"},
        {"num/float-leading-zero", "x = 01.5\n", "前导零"},
        {"str/bad-unicode-range", "s = \"\\UFFFFFFFF\"\n", "超出 Unicode"},
        {"str/us-surrogate", "s = \"\\U0000D800\"\n", "代理区"},
        {"dotted/through-value", "a = 1\na.b = 2\n", "不能穿过"},
        {"dotted/through-implicit-header", "[a.b.c]\nz = 9\n[a]\nb.c.t = 1\n", "不能穿过"},
        {"head/value-under-header", "a = 1\n[a.b]\n", "不能穿过"},
        {"aot/after-implicit", "[[a.b]]\n[[a]]\n", "数组表"},
        {"dt/bad-offset", "d = 1979-05-27T07:32:00+7\n", "两位数字"},
        {"dt/local-with-offset", "d = 07:32:00Z\n", "本地时间"},
    };
}

int report(const ok_case& c, const parse_result& r) {
    if (!r) {
        std::cout << "FAIL " << c.name << "（应解析成功）: " << r.error().line << ':'
                  << r.error().column << ' ' << r.error().message << '\n';
        return 1;
    }
    if (!c.verify(*r)) {
        std::cout << "FAIL " << c.name << "（字段验证失败）\n";
        return 1;
    }
    return 0;
}

int report(const err_case& c, const parse_result& r) {
    if (r) {
        std::cout << "FAIL " << c.name << "（应报错但成功）\n";
        return 1;
    }
    if (r.error().message.find(c.err_contains) == std::string::npos) {
        std::cout << "FAIL " << c.name << "（报错不含“" << c.err_contains
                  << "”）: " << r.error().message << '\n';
        return 1;
    }
    return 0;
}

// ---- Convenience entry points: get<T> / visit / parse_file -------------------

// Completion probe: one overload per variant arm, returns the arm's tag.
struct tag_visitor {
    std::string operator()(const std::string&) const { return "string"; }
    std::string operator()(std::int64_t) const { return "int"; }
    std::string operator()(double) const { return "float"; }
    std::string operator()(bool) const { return "bool"; }
    std::string operator()(const cpptoml::datetime&) const { return "datetime"; }
    std::string operator()(const std::shared_ptr<cpptoml::table>&) const { return "table"; }
    std::string operator()(const std::shared_ptr<cpptoml::array>&) const { return "array"; }
};

// Runs after the parse corpus: each check counts as one case through `expect`.
void api_checks(
    const std::function<void(const char*, bool)>& expect) {
    const parse_result base = cpptoml::parse(
        "n = 42\nf = 1.5\nb = true\ns = \"hi\"\nd = 1979-05-27T07:32:00Z\n"
        "arr = [1]\ntbl = {x = 0}\n");
    if (!base) {
        expect("get/setup (parse of the sample doc failed)", false);
        return;
    }
    const cpptoml::table& root = *base;
    const auto at = [&](const char* key) -> const value& {
        return root.entries.at(key);
    };

    // get<T>: a hit on each of the seven arms, borrowed pointers throughout
    bool hit = true;
    const auto* n = cpptoml::get<std::int64_t>(at("n"));
    const auto* f = cpptoml::get<double>(at("f"));
    const auto* b = cpptoml::get<bool>(at("b"));
    const auto* s = cpptoml::get<std::string>(at("s"));
    const auto* d = cpptoml::get<cpptoml::datetime>(at("d"));
    const auto* arr = cpptoml::get<cpptoml::array>(at("arr"));
    const auto* tbl = cpptoml::get<cpptoml::table>(at("tbl"));
    hit = hit && n != nullptr && *n == 42;
    hit = hit && f != nullptr && *f == 1.5;
    hit = hit && b != nullptr && *b == true;
    hit = hit && s != nullptr && *s == "hi";
    hit = hit && d != nullptr && d->raw == "1979-05-27T07:32:00Z";
    hit = hit && arr != nullptr && arr->items.size() == 1;
    hit = hit && tbl != nullptr && tbl->entries.count("x") == 1;
    expect("get<T>/hit", hit);

    // get<T>: a miss when the arm differs (nullptr, no throw)
    const bool miss = cpptoml::get<double>(at("n")) == nullptr &&
        cpptoml::get<cpptoml::table>(at("n")) == nullptr &&
        cpptoml::get<std::string>(at("f")) == nullptr &&
        cpptoml::get<cpptoml::array>(at("tbl")) == nullptr;
    expect("get<T>/miss", miss);

    // visit: forwarded as-is; every arm lands on its overload
    const auto tag = [&](const value& v) { return cpptoml::visit(tag_visitor{}, v); };
    const bool vis = tag(at("n")) == "int" && tag(at("f")) == "float" &&
        tag(at("b")) == "bool" && tag(at("s")) == "string" &&
        tag(at("d")) == "datetime" && tag(at("tbl")) == "table" &&
        tag(at("arr")) == "array";
    expect("visit/arms", vis);

    // parse_file: write a file, reparse it, verify the top-level table+block
    const char* tmp = "selftest-parse-file.tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "x = 7\n[t.blk]\ny = \"ok\"\n";
    }
    const auto fr = cpptoml::parse_file(tmp);
    const bool file = fr.has_value() && fr->entries.count("x") == 1 &&
        fr->entries.count("t") == 1;
    expect("parse-file/roundtrip", file);
    std::remove(tmp);

    // parse_file: unreadable path yields a document-level error (line=column=0)
    const auto gone = cpptoml::parse_file("selftest-parse-file-missing.tmp");
    const bool missing = !gone.has_value() && gone.error().line == 0 &&
        gone.error().column == 0 && !gone.error().message.empty();
    expect("parse-file/unreadable", missing);
}

}  // namespace

int selftest_main() {
    const auto oks = ok_cases();
    const auto errs = err_cases();
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t total = 0;
    for (auto& c : oks) {
        ++total;
        const auto r = cpptoml::parse(c.toml);
        if (report(c, r) == 0) ++passed; else ++failed;
    }
    for (const auto& c : errs) {
        ++total;
        const auto r = cpptoml::parse(c.toml);
        if (report(c, r) == 0) ++passed; else ++failed;
    }
    // Convenience entry points, one boolean check each.
    const auto expect = [&](const char* name, bool ok) {
        ++total;
        if (ok) {
            ++passed;
        } else {
            ++failed;
            std::cout << "FAIL " << name << '\n';
        }
    };
    api_checks(expect);
    if (failed == 0) {
        std::cout << "selftest: " << passed << '/' << total << " 通过\n";
        return 0;
    }
    std::cout << "selftest: " << passed << '/' << total << " 通过，"
              << failed << " 失败\n";
    return 1;
}
