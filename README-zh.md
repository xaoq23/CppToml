# CppToml (`cpptoml`)

[English](README.md) | 简体中文

面向 **C++23** 的小而轻的 TOML 解析器。零第三方依赖、不抛异常、
没有要配置的分配器——解析文档，得到值树表示;遇到错误时返回列与原因。

- TOML 接受面齐全：表、数组表、点号键、行内表/数组、七种值类型
- 官方 [toml-test](https://github.com/toml-lang/toml-test) TOML 语料
  **717/717**（509 条 invalid 全拒绝，208 条 valid 全接受）
- 免异常通道：`std::expected<table, parse_error>`
- 库代码约 1.3k 行；公共 API 只有三个头

## 五分钟上手

```console
$ git clone <本仓库>
$ cd cpptoml
$ g++ -std=c++23 -Iinclude -o quick_config examples/quick_config.cpp \
      lib/parser.cpp lib/value.cpp lib/format.cpp
$ ./quick_config
```

或用 CMake（顶层构建时选项默认全开）：

```console
$ cmake -B build && cmake --build build
$ ./build/quick_config && ./build/print_tree ./examples/sample.toml
```

要点节选自 `examples/quick_config.cpp`：

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

// 类型化 getter：命中返回指针，类型不符返回 nullptr。
const auto* title = cpptoml::get_string(root.entries.at("title"));

// 数组表元素就是普通表，继续下潜：
const cpptoml::table& db = *cpptoml::get_table(root.entries.at("db"));
const cpptoml::array* ports = cpptoml::get_array(db.entries.at("ports"));

// 整棵树，只读：
std::cout << cpptoml::format_tree(root) << '\n';
```

## 公共 API

全部位于 `namespace cpptoml`。值树以 `shared_ptr` 间接持有以便递归定义；
解析完成后一律只读。

| 头文件 | 内容 |
|---|---|
| `cpptoml/value.hpp` | `value`、`table`、`array`、`datetime` 与 getters（`get_string`/`get_int`/`get_float`/`get_bool`/`get_datetime`/`get_table`/`get_array`）；泛型入口 `get<T>(v)` 与 `visit(visitor, v)` |
| `cpptoml/parse.hpp` | `parse(std::string_view)` 与 `parse_file(std::string_view path)`，返回 `std::expected<table, parse_error>`；`parse_error{line, column, message}` |
| `cpptoml/format.hpp` | `format_value(const value&)`、`format_tree(const table&)` —— 诊断文本，非序列化器 |

注意：

- **`get<T>`** 对照 `std::get_if`，覆盖七种表示类型；`T` 可以是 `table` 或
  `array`（自动拆 shared 持有层，结果为借用指针）。其余类型由
  `static_assert` 在编译期拒绝。
- **`visit`** 是对 `std::visit` 的薄转发，重载集覆盖 `std::string`、
  `std::int64_t`、`double`、`bool`、`datetime`、
  `std::shared_ptr<table>`、`std::shared_ptr<array>`。
- **日期时间保存原始词法**（`datetime::raw` 与输入逐字节一致）。本项目
  不做时区运算与日历格式化，理由见下方定位。
- **诊断文案现为英文**：`message` 字符串与 `format_tree` 的标题行都是。
  客户端应当针对结构化的 `line`/`column` 编程。

## 构建


### [cizon2(详见仓库)](https://github.com/xaoq23/Cizon2)

```console
$ cizon2 build && build\debug-g++\tomlcli.exe selftest
```

### CMake

```console
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
$ ctest --test-dir build
```

选项：`CPPTOML_BUILD_CLI`（tomlcli）、`CPPTOML_BUILD_EXAMPLES`、
`CPPTOML_BUILD_SELFTEST`（接入 CTest）。顶层构建默认全开，经
`add_subdirectory` 嵌入时默认关闭。

## 项目布局

```text
include/cpptoml/   公共 API（value、parse、format）
lib/parser.cpp     递归下降解析器，单趟、无回溯
lib/value.cpp      值的诊断文本表示
lib/format.cpp     表树渲染
lib/detail/        私有头（scanner、数字、字符集、UTF-8、RFC 3339）
src/main.cpp       tomlcli
src/selftest.cpp   内置测试（78 例：语料 + 错误关键词 + API 级）
examples/          quick_config.cpp、print_tree.cpp
```

## 验证

- 官方 [toml-test](https://github.com/toml-lang/toml-test) TOML 1.0 语料：
  **717/717** —— invalid 全拒，valid 全收。
- 内置 selftest：78 例（语法语料、逐条错误关键词匹配、`get<T>` /
  `visit` / `parse_file` 的 API 级用例）。
- g++ 16 与 clang++ 22 双 scope `-Wall -Wextra` 零警告。

## 定位

刻意做小的库。同一语料、同一编译器下与主流选项对照：

| | CppToml | toml++ | toml11 |
|---|---|---|---|
| 语料（TOML 1.0，717 文件） | 717/717 | 717/717 | 716/717 |
| 库代码 | ~1.3k 行 | ~12.7k 行 | ~16.4k 行 |
| 错误通道 | `std::expected` | 异常或免异常开关 | `result` 型 |
| 注释保留 | 不保留 | 保留 | 保留 |
| 序列化/round-trip | 无 | 有（另加 JSON/YAML） | 有（格式感知） |
| datetime 类型 | 原始词法 | 真类型 | 真类型 |

想要最小内核、零依赖、现代 C++ 错误通道，且不需要注释/序列化/时间运算
时，选 CppToml。


## 许可

[Apache-2.0](LICENSE) © CppToml contributors
