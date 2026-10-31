cmake_minimum_required(VERSION 3.21)
project(cpptoml VERSION 0.2.0 LANGUAGES CXX)

# Core library: parser + value model + diagnostic formatter. Zero third-party
# dependencies; C++23 only (std::expected is load-bearing, not optional).
add_library(cpptoml STATIC)
target_sources(cpptoml PRIVATE
    lib/parser.cpp
    lib/value.cpp
    lib/format.cpp)
target_compile_features(cpptoml PUBLIC cxx_std_23)
target_include_directories(cpptoml PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# MSVC is out of scope for now (see README); GNU-family compilers get the
# same warning level the project enforces locally.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cpptoml PRIVATE -Wall -Wextra)
endif()

# Options default to ON only for a top-level build, so embedding
# `add_subdirectory(cpptoml)` stays quiet and builds just the library.
include(CMakeDependentOption)
cmake_dependent_option(CPPTOML_BUILD_CLI
    "Build the tomlcli command-line tool" ON "PROJECT_IS_TOP_LEVEL" OFF)
cmake_dependent_option(CPPTOML_BUILD_EXAMPLES
    "Build the examples/ programs" ON "PROJECT_IS_TOP_LEVEL" OFF)
cmake_dependent_option(CPPTOML_BUILD_SELFTEST
    "Wire the built-in selftest into CTest" ON
    "PROJECT_IS_TOP_LEVEL AND CPPTOML_BUILD_CLI" OFF)

if(CPPTOML_BUILD_CLI)
    add_executable(tomlcli
        src/main.cpp
        src/selftest.cpp)
    target_link_libraries(tomlcli PRIVATE cpptoml)
    target_compile_features(tomlcli PRIVATE cxx_std_23)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tomlcli PRIVATE -Wall -Wextra)
    endif()
endif()

if(CPPTOML_BUILD_EXAMPLES)
    foreach(example IN ITEMS quick_config print_tree)
        add_executable(${example} examples/${example}.cpp)
        target_link_libraries(${example} PRIVATE cpptoml)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${example} PRIVATE -Wall -Wextra)
        endif()
    endforeach()
endif()

include(CTest)
if(CPPTOML_BUILD_SELFTEST AND CPPTOML_BUILD_CLI)
    add_test(NAME cpptoml.selftest COMMAND tomlcli selftest)
endif()
