# CppToml (`cpptoml`)

A small, standards-honest TOML 1.0 parser for **C++23**. No third-party
dependencies, no exceptions, no custom allocators to configure — parse a
document, get a tree, get an error with a line and a column.

- Full TOML 1.0 accepted surface: tables, array of tables, dotted keys,
  inline tables and arrays, and all seven value types
- 717/717 on the official [toml-test](https://github.com/toml-lang/toml-test)
  TOML 1.0 corpus (509 invalid rejected, 208 valid accepted)
- Exception-free: `std::expected<table, parse_error>`
- ~1.3k lines of library code; public API is three headers

## Five minutes

```console
$ git clone <this repo>
$ cd cpptoml
$ g++ -std=c++23 -Iinclude -o quick_config examples/quick_config.cpp \
      lib/parser.cpp lib/value.cpp lib/format.cpp
$ ./quick_config
```

Or with CMake (options default to ON for a top-level build):

```console
$ cmake -B build && cmake --build build
$ ./build/quick_config && ./build/print_tree ./examples/sample.toml
```

The tour, abridged from `examples/quick_config.cpp`:

```cpp
#include <cpptoml/format.hpp>
#include <cpptoml/parse.hpp>
#include <cpptoml/value.hpp>

const auto parsed = cpptoml::parse(R"TOML(
title  = "demo"
[db]
ports = [8001, 8002]

[[servers]]
host = "alpha"

[[servers]]
host = "beta"
)TOML");

if (!parsed) {
    std::cerr << parsed.error().line << ':' << parsed.error().column << ' '
              << parsed.error().message << '\n';
    return 1;
}
const cpptoml::table& root = *parsed;

// Typed getters: pointer on success, nullptr on a type mismatch.
const auto* title = cpptoml::get_string(root.entries.at("title"));

// Array-of-tables elements are ordinary tables; dig in:
const cpptoml::table& db = *cpptoml::get_table(root.entries.at("db"));
const cpptoml::array* ports = cpptoml::get_array(db.entries.at("ports"));

// The whole tree, read-only:
std::cout << cpptoml::format_tree(root) << '\n';
```

## Public API

All names live in `namespace cpptoml`. Value trees are
`shared_ptr`-linked so the types can recurse; everything is read-only after
parse.

| Header | Names |
|---|---|
| `cpptoml/value.hpp` | `value`, `table`, `array`, `datetime` and the getters `get_string`/`get_int`/`get_float`/`get_bool`/`get_datetime`/`get_table`/`get_array`; generic accessors `get<T>(v)` and `visit(visitor, v)` |
| `cpptoml/parse.hpp` | `parse(std::string_view)` and `parse_file(std::string_view path)` returning `std::expected<table, parse_error>`; `parse_error{line, column, message}` |
| `cpptoml/format.hpp` | `format_value(const value&)` and `format_tree(const table&)` — diagnostic text, not a serializer |

Notes:

- **`get<T>`** mirrors `std::get_if` for the seven representation types;
  `T` may be `table` or `array` (the shared holder is unwrapped, the result
  is borrowed). Anything else fails at compile time via `static_assert`.
- **`visit`** is a thin forwarding wrapper over `std::visit`, so the overload
  set covers `std::string`, `std::int64_t`, `double`, `bool`, `datetime`,
  `std::shared_ptr<table>` and `std::shared_ptr<array>`.
- **Datetimes are raw tokens** (`datetime::raw` prints exactly what you
  wrote). The project does not do timezone math or calendar formatting; see
  the position paper below for why.
- **Diagnostics today are Chinese**: `message` strings and `format_tree`'s
  banner. The structured `line`/`column` is the contract clients should
  program against.

## Building

Three doors, in increasing order of ceremony — all produce the same library.

### cizon2 (first-class)

```console
$ cizon2 build && build\debug-g++\tomlcli.exe selftest
```

### CMake

```console
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
$ ctest --test-dir build
```

Options: `CPPTOML_BUILD_CLI` (tomlcli), `CPPTOML_BUILD_EXAMPLES`,
`CPPTOML_BUILD_SELFTEST` (CTest wire-up). All default ON for top-level
builds and OFF when embedded via `add_subdirectory`.

### Amalgamated (`dist/`)

For projects that just want to drop two files in:

```console
$ python tools/amalgamate.py    # regenerates dist/ deterministically
```

- `dist/cpptoml.hpp` — all public headers plus the private `detail/` modules
  inlined into one header
- `dist/cpptoml.cpp` — all translation units merged; includes `cpptoml.hpp`
  once; no build system needed
- `dist/cpptoml/{value,parse,format}.hpp` — shims so `-Idist` keeps
  `<cpptoml/parse.hpp>`-style includes compiling

```console
$ g++ -std=c++23 -Idist -o app app.cpp dist/cpptoml.cpp
```

## Project layout

```text
include/cpptoml/   public API (value, parse, format)
lib/parser.cpp     recursive-descent parser, one pass, no backtracking
lib/value.cpp      value repr for diagnostics
lib/format.cpp     table tree renderer
lib/detail/        private headers (scanner, numbers, charsets, UTF-8, RFC 3339)
src/main.cpp       tomlcli
src/selftest.cpp   built-in test suite (78 cases: valid corpus + error keywords + API)
examples/          quick_config.cpp, print_tree.cpp
tools/amalgamate.py
```

## Verification

- Official [toml-test](https://github.com/toml-lang/toml-test) TOML 1.0
  corpus: **717/717** — every `invalid` case rejected, every `valid` case
  accepted.
- Built-in selftest: 78 checks (syntax corpus, per-error keyword matching,
  API-level tests for `get<T>` / `visit` / `parse_file`).
- Warnings-clean at `-Wall -Wextra` on g++ 16 and clang++ 22, both scopes.

## Positioning

Ours is a deliberately small library. Compared with the mainstream options
under the same corpus and compiler:

| | CppToml | toml++ | toml11 |
|---|---|---|---|
| Corpus (TOML 1.0, 717 files) | 717/717 | 717/717 | 716/717 |
| Library code | ~1.3k lines | ~12.7k lines | ~16.4k lines |
| Error channel | `std::expected` | exceptions or opt-out | `result`-style |
| Comments preserved | no | yes | yes |
| Serialization/round-trip | no | yes (+ JSON/YAML) | yes (format-aware) |
| Datetime type | raw token | real types | real types |

Reach for CppToml when you want a minimal, dependency-free parser with a
modern-C++ error channel, and you do not need comments, serialization, or
datetime arithmetic.

## Not goals

- Header-only distribution (amalgamation covers you instead)
- Comment preservation, serialization, JSON/YAML emitters
- Datetime arithmetic (values keep the raw token)
- Compilers without C++23 (`std::expected` is part of the public contract)
- Multi-config build systems other than the three doors above

## License

[MIT](LICENSE) © CppToml contributors
