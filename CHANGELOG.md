# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project does not use semantic versioning yet; breaking API changes
are flagged explicitly in each entry.

## [0.2.1] - 2026-11-01

### Changed

- All CLI and parse diagnostics translated to English: `parse_error`
  messages (including the RFC 3339 reasons), the `format_tree` banner,
  `tomlcli` output and the built-in selftest report.
- Re-licensed under Apache-2.0 (was MIT); `LICENSE` updated.
- Docs: cross-language navigation links at the top of `README.md` and
  `README-zh.md`; English README synced with the Chinese edits (intro
  wording, dropped "1.0" qualification in feature bullets, linked the
  cizon2 build heading to its repo, removed the "Not goals" section).

### Removed

- The amalgamated distribution `dist/` and its generator
  `tools/amalgamate.py`; the library ships as plain sources
  (`include/` + `lib/`) built through CMake or cizon2.

## [0.2.0] - 2026-11-01

### Added

- `cpptoml::parse_file(std::string_view path)` — read a file (binary mode,
  BOM handled) and parse it; an unreadable file yields a parse error with
  `line = column = 0`.
- `cpptoml::visit(visitor, const value&)` — thin forwarding wrapper over
  `std::visit`, one arm per variant alternative.
- `cpptoml::get<T>(const value&)` — generic access mirroring `std::get_if`;
  `T` may be `table`/`array` after unwrapping the shared holder, and anything
  else is a compile-time error via `static_assert`.
- `cpptoml::is_value_type_v<T>` — compile-time membership test for the seven
  value types.

### Changed

- Comments throughout the library translated to English; a redundancy audit
  dropped dead code and merged repeated helpers with no behavior change.
- Distribution: `tools/amalgamate.py` generates an amalgamated
  `dist/cpptoml.hpp` + `dist/cpptoml.cpp` (plus shim headers under
  `dist/cpptoml/`) for projects without a package manager.

### Added (packaging)

- Root `CMakeLists.txt` (static library, optional `tomlcli` and examples,
  CTest wire-up for the built-in selftest).
- English `README.md` with a five-minute example, a Chinese mirror
  `README-zh.md`, and `examples/` (quick_config, print_tree).

## [0.1.0] - 2026-10-31

### Added

- First public release.
- Complete TOML 1.0 parser: tables, array of tables, dotted keys, inline
  tables and arrays, the seven value types (string, integer, float, boolean,
  date-time, array, table); datetimes are stored as their raw token.
- Exception-free error handling via `std::expected<table, parse_error>`,
  errors carry a 1-based line/column and an exact reason.
- `cpptoml::format_tree(const table&)` plus typed getters
  (`get_string`/`get_int`/`get_float`/`get_bool`/`get_datetime`/
  `get_table`/`get_array`).
- `tomlcli`, a small CLI: parse-and-print, `check` (validate only),
  `selftest` (built-in corpus) and `version`.
- Verification: 717/717 on the official toml-test TOML 1.0 corpus
  (509 invalid all rejected, 208 valid all accepted) and a built-in
  selftest suite; warnings-clean under `-Wall -Wextra` on g++ and clang++.
