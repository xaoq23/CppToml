// lib/parser.cpp — TOML 1.0 recursive-descent parser.
//
// Structure: parse() pre-scans for UTF-8 validity and control bytes, then
// runs the document loop (comments / table headers / key-value pairs).
// Table semantics are tracked in two parallel structures:
//   1) the public value tree: leaves hold a value; AOT elements are
//      shared_ptr<table>;
//   2) node_states: sid (canonical path; after descending into an AOT
//      element a "@<index>" suffix is appended) → node kind.
// Five kinds: implicit_header / implicit_kv / header / aot / value.
// Rules (aligned with the spec and the corpus):
//   header walks: fresh/implicit_header/implicit_kv/header are allowed; an
//      aot descends into its newest element; a plain value is rejected;
//   dotted walks: only fresh (recorded as implicit_kv) and implicit_kv; any
//      other kind is illegal to pass through;
//   header last segment [x]: fresh → header; implicit_header → upgraded;
//      header → duplicate definition; implicit_kv/value → key-value
//      definition conflict; aot → array-of-tables conflict;
//   header last segment [[x]]: fresh → aot; aot → appended; anything else
//      → array-of-tables conflict;
//   assignment last segment: must be brand new, otherwise the duplicate-key
//      diagnostic fires.
// Inline tables carry their own local states and freeze once complete (at
// the value-tree level they can be replaced as a whole but never descended).

#include <cpptoml/parse.hpp>

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "detail/charset.hpp"
#include "detail/rfc3339.hpp"
#include "detail/scanner.hpp"
#include "detail/strnum.hpp"
#include "detail/utf8.hpp"

namespace cpptoml {
namespace {

using detail::scanner;
using detail::parse_abort;

namespace strnum = detail::strnum;
namespace charset = detail::charset;
namespace utf8 = detail::utf8;

/// Internal separator of the canonical path. It never appears in bare TOML
/// keys; quoted keys could theoretically collide (the corpus has no such
/// case).
constexpr char SEP = '\x1f';

enum class node_kind { implicit_header, implicit_kv, header, aot, value };

struct node_state {
    node_kind kind = node_kind::value;
    std::size_t count = 0;  // aot only: element count
};

const char* kind_word(node_kind k) {
    switch (k) {
        case node_kind::implicit_header: return "an implicit table";
        case node_kind::implicit_kv: return "a table created by key-value pairs";
        case node_kind::header: return "a table";
        case node_kind::aot: return "an array of tables";
        case node_kind::value: return "a key-value pair";
    }
    return "a node";
}

std::string pretty(std::string_view sid) {
    std::string out;
    if (!sid.empty() && sid.front() == SEP) sid.remove_prefix(1);  // top-level keys carry no leading dot
    for (const char c : sid) {
        if (c == SEP) out.push_back('.');
        else if (c == '@') out.push_back('@');  // the @index suffix survives display
        else out.push_back(c);
    }
    return out;
}

std::string join(const std::string& base, const std::string& part) {
    return base + SEP + part;  // unconditional SEP: an empty segment ('') must not collapse upward
}

struct builder {
    std::shared_ptr<table> root = std::make_shared<table>();
    std::unordered_map<std::string, node_state> states;

    /// Fetches the shared_ptr<table> under a parent from the value tree;
    /// the value must already be in table form.
    static const std::shared_ptr<table>& as_table(const value& v, scanner& s, std::string_view where) {
        const auto p = std::get_if<std::shared_ptr<table>>(&v);
        if (p == nullptr) {
            detail::fail(s, "internal error: value tree node is not a table (" + std::string(where) + ")");
        }
        return *p;
    }
};

struct arena_ref {
    std::unordered_map<std::string, node_state>& states;
    std::shared_ptr<table> root;
    std::string base_sid;
};

// ---- Forward declarations (value bodies live further down) ------------------

value parse_value_expr(scanner& s);                    // value dispatch (recursive arrays/inline)
value parse_inline(scanner& s);                        // inline table (its own local arena)
[[nodiscard]] std::vector<std::string> parse_key(scanner& s);  // dotted key

// ---- Walking -----------------------------------------------------------------

struct walk_out {
    std::shared_ptr<table> parent;
    std::string sid;
};

/// Consumes parts[0..n-2]. With header set, header-walk rules apply;
/// otherwise the dotted-key rules apply.
walk_out walk_intermediate(arena_ref a, const std::vector<std::string>& parts, bool header_walk, scanner& s) {
    std::shared_ptr<table> cur = a.root;
    std::string sid = a.base_sid;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& p = parts[i];
        std::string node_sid = join(sid, p);
        node_state ns{};
        const bool fresh = [&]{
            const auto it = a.states.find(node_sid);
            if (it == a.states.end()) {
                ns = {header_walk ? node_kind::implicit_header : node_kind::implicit_kv, 0};
                a.states.emplace(node_sid, ns);
                cur->entries[p] = std::make_shared<table>();
                return true;
            }
            ns = it->second;
            return false;
        }();
        if (fresh) {
            cur = builder::as_table(cur->entries[p], s, "fresh-child");
            sid = node_sid;
            continue;
        }
        if (ns.kind == node_kind::aot && header_walk) {
            const auto& arr = std::get<std::shared_ptr<array>>(cur->entries[p]);
            if (arr->items.empty()) detail::fail(s, "internal error: empty array of tables");
            cur = std::get<std::shared_ptr<table>>(arr->items.back());
            sid = node_sid + "@" + std::to_string(arr->items.size() - 1);
            continue;
        }
        if (!header_walk && ns.kind == node_kind::implicit_kv) {
            cur = builder::as_table(cur->entries[p], s, "implicit-kv-child");
            sid = node_sid;
            continue;
        }
        if (header_walk && (ns.kind == node_kind::implicit_kv || ns.kind == node_kind::implicit_header ||
                            ns.kind == node_kind::header)) {
            // header walks may pass through implicit_* / header (aot was
            // descended into above)
            cur = builder::as_table(cur->entries[p], s, "existing-child");
            sid = node_sid;
            continue;
        }
        detail::fail(s, "«" + pretty(node_sid) + "» is already defined as " + kind_word(ns.kind) +
                        "; keys or table headers cannot pass through it");
    }
    return {cur, sid};
}

/// Closes the last segment as a key-value assignment (shared by KV rows and
/// inline members; the local arena obeys the same rule).
void assign_leaf(arena_ref a, const std::vector<std::string>& parts, const value& v, scanner& s) {
    const walk_out w = walk_intermediate(a, parts, false, s);
    const std::string end_sid = join(w.sid, parts.back());
    if (a.states.count(end_sid)) {
        detail::fail(s, "duplicate key «" + pretty(end_sid) + "»");
    }
    a.states[end_sid] = {node_kind::value, 0};
    w.parent->entries[parts.back()] = v;
}

// ---- Key parsing --------------------------------------------------------------

/// One key segment: a bare key or a single-line quoted string. Failures
/// report under the key-name diagnostic.
std::string parse_key_part(scanner& s) {
    if (s.peek() == '"') return strnum::scan_basic_string(s);
    if (s.peek() == '\'') return strnum::scan_literal_string(s);
    if (!charset::is_bare_key_char(s.peek())) {
        const char c = s.peek();
        detail::fail(s, std::string("key name missing or contains an illegal character: ") +
                        (c ? std::string("'") + c + "'" : "non-ASCII byte"));
    }
    std::string p;
    while (charset::is_bare_key_char(s.peek())) {
        p.push_back(s.peek());
        s.bump();
    }
    return p;
}

/// Dotted key (space around the dots is skipped).
std::vector<std::string> parse_key(scanner& s) {
    std::vector<std::string> parts;
    for (;;) {
        s.skip_spaces();
        parts.push_back(parse_key_part(s));
        s.skip_spaces();
        if (s.peek() == '.') { s.bump(); continue; }
        return parts;
    }
}

// ---- Arrays --------------------------------------------------------------------

value parse_array(scanner& s) {
    s.bump();  // '['
    auto arr = std::make_shared<array>();
    for (;;) {
        detail::skip_ws_comments_lines(s);
        if (s.eof()) detail::fail(s, "unterminated array (missing ']')");
        if (s.peek() == ']') { s.bump(); return arr; }
        arr->items.push_back(parse_value_expr(s));
        detail::skip_ws_comments_lines(s);
        if (s.eof()) detail::fail(s, "unterminated array (missing ']')");
        if (s.peek() == ',') { s.bump(); continue; }
        if (s.peek() == ']') { s.bump(); return arr; }
        detail::fail(s, "expected ',' or ']' inside the array");
    }
}

// ---- Value dispatch -------------------------------------------------------------

value parse_value_expr(scanner& s) {
    if (s.eof() || s.at_newline() || s.peek() == '#') detail::fail(s, "missing value");
    const char c = s.peek();
    switch (c) {
        case '"':
            if (s.peek_at(1) == '"' && s.peek_at(2) == '"') {
                return value{strnum::scan_multiline_string(s, true)};
            }
            return value{strnum::scan_basic_string(s)};
        case '\'':
            if (s.peek_at(1) == '\'' && s.peek_at(2) == '\'') {
                return value{strnum::scan_multiline_string(s, false)};
            }
            return value{strnum::scan_literal_string(s)};
        case '[':
            return value{parse_array(s)};
        case '{':
            return value{parse_inline(s)};
        case '+':
        case '-':
            return strnum::scan_number(s);
        case 't':
        case 'f':
            return value{strnum::scan_bool(s)};
        default: break;
    }
    // Datetime look-ahead: yyyy- or hh: (other digits go to number scanning).
    if (detail::rfc::looks_like_datetime(s)) {
        return value{strnum::scan_datetime(s)};
    }
    if (charset::is_digit(c) || c == 'i' || c == 'n') {
        return strnum::scan_number(s);
    }
    detail::fail(s, std::string("unrecognized value: '") + c + "'");
}

// ---- Inline tables ---------------------------------------------------------------

value parse_inline(scanner& s) {
    s.bump();  // '{'
    auto root = std::make_shared<table>();
    std::unordered_map<std::string, node_state> local_states;
    arena_ref a{local_states, root, ""};  // local arena: self-contained inside the inline table

    s.skip_spaces();
    if (s.eof() || s.at_newline()) detail::fail(s, "inline tables must stay on a single line");
    if (s.peek() == '}') { s.bump(); return root; }
    for (;;) {
        s.skip_spaces();
        if (s.eof() || s.at_newline()) detail::fail(s, "inline tables must stay on a single line");
        const std::vector<std::string> parts = parse_key(s);
        s.skip_spaces();
        if (s.peek() != '=') detail::fail(s, "'=' required after a key inside an inline table");
        s.bump();
        s.skip_spaces();
        if (s.eof() || s.at_newline()) detail::fail(s, "missing value");
        const value v = parse_value_expr(s);
        assign_leaf(a, parts, v, s);
        s.skip_spaces();
        if (s.peek() == ',') {
            s.bump();
            s.skip_spaces();
            if (s.eof() || s.at_newline()) detail::fail(s, "inline tables must stay on a single line");
            if (s.peek() == '}') detail::fail(s, "inline tables do not allow a trailing comma");
            continue;
        }
        if (s.peek() == '}') { s.bump(); return root; }
        if (s.eof() || s.at_newline()) detail::fail(s, "inline tables must stay on a single line");
        detail::fail(s, "expected ',' or '}' inside the inline table");
    }
}

// ---- Table headers -----------------------------------------------------------------

/// Line tail: only spaces, a comment, or newline/EOF are allowed; a comment
/// is consumed to the end of the line but the newline is not.
void expect_line_end(scanner& s) {
    s.skip_spaces();
    if (s.eof() || s.at_newline()) return;
    if (s.peek() == '#') {
        detail::skip_comment(s);
        return;  // the document loop handles the newline/EOF after the comment
    }
    detail::fail(s, "unexpected content (only whitespace or a comment may follow on the line)");
}

struct section {
    std::shared_ptr<table> tbl;  // where this section's later key-value pairs land
    std::string sid;             // canonical path of this section (AOT's current element carries @i)
};

/// Applies header semantics: walking is already done; classifies the last
/// segment and returns the new current section.
section apply_header(builder& b, const std::vector<std::string>& parts, bool aot_form, scanner& s) {
    const arena_ref a{b.states, b.root, ""};
    const walk_out w = walk_intermediate(a, parts, true, s);
    const std::string end_sid = join(w.sid, parts.back());
    auto st = b.states.find(end_sid);

    if (!aot_form) {
        if (st == b.states.end()) {
            w.parent->entries[parts.back()] = std::make_shared<table>();
            b.states.emplace(end_sid, node_state{node_kind::header, 0});
            return {builder::as_table(w.parent->entries[parts.back()], s, "header-new"), end_sid};
        }
        switch (st->second.kind) {
            case node_kind::implicit_header:
                st->second.kind = node_kind::header;
                return {builder::as_table(w.parent->entries[parts.back()], s, "header-upgrade"),
                        end_sid};
            case node_kind::header:
                detail::fail(s, "duplicate definition of table «" + pretty(end_sid) + "»");
            case node_kind::implicit_kv:
            case node_kind::value:
                detail::fail(s, "«" + pretty(end_sid) + "» is already defined by a key-value pair; it cannot be reopened as a table header");
            case node_kind::aot:
                detail::fail(s, "«" + pretty(end_sid) + "» is an array of tables; it cannot be redefined by a table header");
        }
        detail::fail(s, "internal error: unknown node kind");
    }

    if (st == b.states.end()) {
        auto arr = std::make_shared<array>();
        arr->items.push_back(std::make_shared<table>());
        w.parent->entries[parts.back()] = arr;
        b.states.emplace(end_sid, node_state{node_kind::aot, 1});
        return {std::get<std::shared_ptr<table>>(arr->items.back()), end_sid + "@0"};
    }
    if (st->second.kind == node_kind::aot) {
        auto& arr = std::get<std::shared_ptr<array>>(w.parent->entries[parts.back()]);
        arr->items.push_back(std::make_shared<table>());
        ++st->second.count;
        const std::string sid = end_sid + "@" + std::to_string(arr->items.size() - 1);
        return {std::get<std::shared_ptr<table>>(arr->items.back()), sid};
    }
    detail::fail(s, "«" + pretty(end_sid) + "» is already " + std::string(kind_word(st->second.kind)) +
                        "; it cannot be redefined as an array of tables");
}

/// '[' / '[[' headers: parses the key path, the closing brackets, checks
/// the line tail, and applies the section semantics.
section parse_header(scanner& s, builder& b) {
    s.bump();  // '['
    bool aot_form = false;
    if (s.peek() == '[') {
        aot_form = true;
        s.bump();
    }
    const std::vector<std::string> parts = parse_key(s);
    s.skip_spaces();
    if (aot_form) {
        if (s.peek() != ']' || s.peek_at(1) != ']') detail::fail(s, "table header requires ']]'");
        s.bump_n(2);
    } else {
        if (s.peek() != ']') detail::fail(s, "table header requires ']'");
        s.bump();
    }
    expect_line_end(s);
    return apply_header(b, parts, aot_form, s);
}

}  // namespace

std::expected<table, parse_error> parse(std::string_view source) {
    std::string_view text = source;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    try {
        if (const utf8::check_result r = utf8::check(text); !r.ok) {
            const auto [line, col] = detail::line_col_of(text, r.bad);
            return std::unexpected(parse_error{
                line, col, std::string("invalid UTF-8 sequence or control character")});
        }
        builder b;
        scanner s{text};
        section cur{b.root, ""};
        for (;;) {
            s.skip_spaces();
            if (s.eof()) break;
            if (s.at_newline()) {
                s.bump_newline();
                continue;
            }
            if (s.peek() == '#') {
                detail::skip_comment(s);
                continue;
            }
            if (s.peek() == '[') {
                cur = parse_header(s, b);
                continue;
            }
            const std::vector<std::string> parts = parse_key(s);
            s.skip_spaces();
            if (s.peek() != '=') detail::fail(s, "a key must be followed by '='");
            s.bump();
            s.skip_spaces();
            const value v = parse_value_expr(s);
            expect_line_end(s);
            assign_leaf(arena_ref{b.states, cur.tbl, cur.sid}, parts, v, s);
        }
        return std::move(*b.root);
    } catch (const parse_abort& e) {
        return std::unexpected(parse_error{e.line, e.col, std::move(e.msg)});
    }
}

// Reads the whole file in binary mode, then hands the text to parse(); BOM,
// UTF-8 and control-byte handling therefore match parse() exactly.
std::expected<table, parse_error> parse_file(const std::string_view path) {
    std::ifstream in{std::string(path), std::ios::binary};
    if (!in) {
        return std::unexpected(parse_error{0, 0, "cannot open file: " + std::string(path)});
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

}  // namespace cpptoml
