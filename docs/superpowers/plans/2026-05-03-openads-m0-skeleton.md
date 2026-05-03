# OpenADS — M0 Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring up the OpenADS repository skeleton so subsequent milestones can land code: a buildable C++17 CMake project with a portable L5 platform layer (file I/O, byte-range locking, mmap, paths, time, threads), a small `util` layer (`Result<T>`, `Span<T>`, structured `Log`), public-header placeholders, a doctest unit-test harness, and a GitHub Actions CI matrix covering Windows / Linux / macOS.

**Architecture:** Pure C++17 with `extern "C"` ABI reserved for the future L1 layer. A thin POSIX / Win32 split inside `src/platform/`, header-driven public API under `include/openads/`. Doctest is vendored header-only under `third_party/doctest/`. CMake top-level orchestrates a single library target `openads_core` plus a `openads_unit_tests` executable. No external runtime dependencies in M0.

**Tech Stack:** C++17, CMake ≥ 3.20, doctest 2.4.x (header-only, MIT, vendored), GitHub Actions, MSVC 2022 / GCC 11+ / Clang 14+, MinGW-w64 optional.

---

## File structure for this milestone

Created in M0:

```
OpenADS/
├── .editorconfig
├── .gitattributes
├── .gitignore
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── third_party/
│   └── doctest/
│       ├── README.md
│       └── doctest.h
├── include/
│   └── openads/
│       ├── error.h
│       ├── version.h
│       └── platform.h          # forward declarations only
├── src/
│   ├── util/
│   │   ├── result.h
│   │   ├── span.h
│   │   ├── log.h
│   │   └── log.cpp
│   ├── platform/
│   │   ├── file.h
│   │   ├── file_win32.cpp
│   │   ├── file_posix.cpp
│   │   ├── lock.h
│   │   ├── lock_win32.cpp
│   │   ├── lock_posix.cpp
│   │   ├── mmap.h
│   │   ├── mmap_win32.cpp
│   │   ├── mmap_posix.cpp
│   │   ├── path.h
│   │   ├── path.cpp
│   │   ├── time.h
│   │   ├── time.cpp
│   │   ├── thread.h
│   │   └── thread.cpp
│   └── CMakeLists.txt
├── tests/
│   ├── CMakeLists.txt
│   └── unit/
│       ├── doctest_main.cpp
│       ├── util_result_test.cpp
│       ├── util_span_test.cpp
│       ├── util_log_test.cpp
│       ├── platform_file_test.cpp
│       ├── platform_lock_test.cpp
│       ├── platform_mmap_test.cpp
│       ├── platform_path_test.cpp
│       ├── platform_time_test.cpp
│       └── platform_thread_test.cpp
└── .github/
    └── workflows/
        └── ci.yml
```

Boundaries:

- `util/` knows nothing about OS or files.
- `platform/` is the single OS dependency. Everything outside `platform/` calls these wrappers; nothing else includes `<windows.h>` or POSIX headers.
- `include/openads/` exposes only what the L1 ABI will eventually need; in M0 these are minimal types so downstream milestones can include the headers.
- `tests/unit/` mirrors source paths; one test file per source file when reasonable.

---

## Task 1: Repository bootstrap files

**Files:**
- Create: `c:/OpenADS/LICENSE`
- Create: `c:/OpenADS/.gitignore`
- Create: `c:/OpenADS/.gitattributes`
- Create: `c:/OpenADS/.editorconfig`

- [ ] **Step 1: Write `LICENSE` (MIT)**

```
MIT License

Copyright (c) 2026 OpenADS contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

- [ ] **Step 2: Write `.gitignore`**

```
# Build output
build/
build-*/
out/
cmake-build-*/

# CMake
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
Makefile
*.cmake
!CMakePresets.json
!third_party/**/*.cmake

# IDE
.vs/
.vscode/
.idea/
*.user

# OS
.DS_Store
Thumbs.db

# Compiler / linker artefacts
*.obj
*.o
*.a
*.so
*.dll
*.dylib
*.exe
*.exp
*.lib
*.pdb
*.ilk
```

- [ ] **Step 3: Write `.gitattributes`**

```
* text=auto eol=lf
*.bat   text eol=crlf
*.cmd   text eol=crlf
*.ps1   text eol=crlf
*.png   binary
*.jpg   binary
*.gif   binary
*.dbf   binary
*.cdx   binary
*.ntx   binary
*.adt   binary
*.adi   binary
*.adm   binary
*.fpt   binary
*.dbt   binary
```

- [ ] **Step 4: Write `.editorconfig`**

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
indent_style = space
indent_size = 4
insert_final_newline = true
trim_trailing_whitespace = true

[*.{md,yml,yaml}]
indent_size = 2

[*.{bat,cmd,ps1}]
end_of_line = crlf
```

- [ ] **Step 5: Commit**

```
git add LICENSE .gitignore .gitattributes .editorconfig
git commit -m "chore: bootstrap repository with MIT license and editor config"
```

---

## Task 2: CMake skeleton with vendored doctest

**Files:**
- Create: `c:/OpenADS/CMakeLists.txt`
- Create: `c:/OpenADS/CMakePresets.json`
- Create: `c:/OpenADS/src/CMakeLists.txt`
- Create: `c:/OpenADS/tests/CMakeLists.txt`
- Create: `c:/OpenADS/third_party/doctest/doctest.h` (downloaded from upstream)
- Create: `c:/OpenADS/third_party/doctest/README.md`
- Create: `c:/OpenADS/tests/unit/doctest_main.cpp`

- [ ] **Step 1: Vendor doctest**

Download `doctest.h` 2.4.11 (single-header) from `https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h` to `c:/OpenADS/third_party/doctest/doctest.h`.

Write `c:/OpenADS/third_party/doctest/README.md`:

```
# doctest

Vendored single-header build of doctest 2.4.11 (MIT License).

Source: https://github.com/doctest/doctest

Update procedure: replace `doctest.h` with the new release header,
update version above, run the unit-test suite.
```

- [ ] **Step 2: Write the top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)

project(OpenADS
    VERSION 0.0.1
    LANGUAGES CXX
    DESCRIPTION "Open-source ADS-compatible engine"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(OPENADS_BUILD_TESTS "Build OpenADS unit tests" ON)
option(OPENADS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

add_subdirectory(src)

if(OPENADS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Write `c:/OpenADS/src/CMakeLists.txt`**

```cmake
add_library(openads_core STATIC
    util/log.cpp
    platform/path.cpp
    platform/time.cpp
    platform/thread.cpp
)

if(WIN32)
    target_sources(openads_core PRIVATE
        platform/file_win32.cpp
        platform/lock_win32.cpp
        platform/mmap_win32.cpp
    )
else()
    target_sources(openads_core PRIVATE
        platform/file_posix.cpp
        platform/lock_posix.cpp
        platform/mmap_posix.cpp
    )
endif()

target_include_directories(openads_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(UNIX)
    target_link_libraries(openads_core PUBLIC pthread)
endif()
```

- [ ] **Step 4: Write `c:/OpenADS/tests/CMakeLists.txt`**

```cmake
add_executable(openads_unit_tests
    unit/doctest_main.cpp
    unit/util_result_test.cpp
    unit/util_span_test.cpp
    unit/util_log_test.cpp
    unit/platform_file_test.cpp
    unit/platform_lock_test.cpp
    unit/platform_mmap_test.cpp
    unit/platform_path_test.cpp
    unit/platform_time_test.cpp
    unit/platform_thread_test.cpp
)

target_include_directories(openads_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(openads_unit_tests PRIVATE openads_core)

add_test(NAME openads_unit_tests COMMAND openads_unit_tests)
```

- [ ] **Step 5: Write `c:/OpenADS/tests/unit/doctest_main.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 6: Write `c:/OpenADS/CMakePresets.json`**

```json
{
    "version": 4,
    "configurePresets": [
        {
            "name": "default",
            "displayName": "Default Release",
            "binaryDir": "${sourceDir}/build/default",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        },
        {
            "name": "debug",
            "inherits": "default",
            "binaryDir": "${sourceDir}/build/debug",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug"
            }
        },
        {
            "name": "msvc-x64",
            "inherits": "default",
            "generator": "Visual Studio 17 2022",
            "architecture": "x64",
            "binaryDir": "${sourceDir}/build/msvc-x64"
        },
        {
            "name": "ninja-clang",
            "inherits": "default",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/ninja-clang",
            "cacheVariables": {
                "CMAKE_C_COMPILER": "clang",
                "CMAKE_CXX_COMPILER": "clang++"
            }
        }
    ],
    "buildPresets": [
        { "name": "default", "configurePreset": "default" },
        { "name": "debug",   "configurePreset": "debug"   }
    ],
    "testPresets": [
        {
            "name": "default",
            "configurePreset": "default",
            "output": { "outputOnFailure": true }
        }
    ]
}
```

- [ ] **Step 7: Stub the empty test files so the build succeeds**

For each of the nine test files in `tests/CMakeLists.txt` (other than `doctest_main.cpp`), create a placeholder containing only:

```cpp
#include "doctest.h"
```

Files to create with that single-line content:

- `c:/OpenADS/tests/unit/util_result_test.cpp`
- `c:/OpenADS/tests/unit/util_span_test.cpp`
- `c:/OpenADS/tests/unit/util_log_test.cpp`
- `c:/OpenADS/tests/unit/platform_file_test.cpp`
- `c:/OpenADS/tests/unit/platform_lock_test.cpp`
- `c:/OpenADS/tests/unit/platform_mmap_test.cpp`
- `c:/OpenADS/tests/unit/platform_path_test.cpp`
- `c:/OpenADS/tests/unit/platform_time_test.cpp`
- `c:/OpenADS/tests/unit/platform_thread_test.cpp`

These will gain real content in the tasks below.

- [ ] **Step 8: Stub source files referenced by `src/CMakeLists.txt` so the build succeeds**

Create empty placeholders that compile. Each contains the single line:

```cpp
// placeholder, real content lands in a later task
```

Files:

- `c:/OpenADS/src/util/log.cpp`
- `c:/OpenADS/src/platform/path.cpp`
- `c:/OpenADS/src/platform/time.cpp`
- `c:/OpenADS/src/platform/thread.cpp`
- `c:/OpenADS/src/platform/file_win32.cpp`
- `c:/OpenADS/src/platform/file_posix.cpp`
- `c:/OpenADS/src/platform/lock_win32.cpp`
- `c:/OpenADS/src/platform/lock_posix.cpp`
- `c:/OpenADS/src/platform/mmap_win32.cpp`
- `c:/OpenADS/src/platform/mmap_posix.cpp`

- [ ] **Step 9: Configure and build**

Run from `c:/OpenADS`:

```
cmake --preset default
cmake --build build/default
```

Expected: configure succeeds, build succeeds, target `openads_unit_tests` produced.

- [ ] **Step 10: Run the empty test suite**

Run:

```
ctest --preset default
```

Expected: `1/1 Test #1: openads_unit_tests ... Passed`. The doctest binary returns success when there are no test cases.

- [ ] **Step 11: Commit**

```
git add CMakeLists.txt CMakePresets.json src/CMakeLists.txt tests/CMakeLists.txt third_party/doctest/ tests/unit/ src/util/ src/platform/
git commit -m "build: CMake skeleton with vendored doctest harness"
```

---

## Task 3: `util/Result<T>` — error-or-value type

**Files:**
- Create: `c:/OpenADS/src/util/result.h`
- Modify: `c:/OpenADS/tests/unit/util_result_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace the contents of `c:/OpenADS/tests/unit/util_result_test.cpp`:

```cpp
#include "doctest.h"
#include "util/result.h"

using openads::util::Error;
using openads::util::Result;

TEST_CASE("Result holds a value when constructed from one") {
    Result<int> r{42};
    CHECK(r.has_value());
    CHECK(r.value() == 42);
    CHECK(static_cast<bool>(r));
}

TEST_CASE("Result holds an error when constructed from one") {
    Result<int> r{Error{5012, 0, "locked", ""}};
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 5012);
    CHECK(r.error().message == "locked");
}

TEST_CASE("Result<void> distinguishes ok from error") {
    Result<void> ok;
    CHECK(ok.has_value());

    Result<void> err{Error{4001, 0, "net", ""}};
    CHECK_FALSE(err.has_value());
    CHECK(err.error().code == 4001);
}

TEST_CASE("OPENADS_TRY propagates errors") {
    auto produce_err = []() -> Result<int> {
        return Error{5004, 0, "not impl", ""};
    };
    auto wrap = [&]() -> Result<int> {
        OPENADS_TRY(int v, produce_err());
        return v + 1;
    };
    auto r = wrap();
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 5004);
}

TEST_CASE("OPENADS_TRY forwards the value when ok") {
    auto produce_ok = []() -> Result<int> { return 7; };
    auto wrap = [&]() -> Result<int> {
        OPENADS_TRY(int v, produce_ok());
        return v + 1;
    };
    auto r = wrap();
    REQUIRE(r.has_value());
    CHECK(r.value() == 8);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `util/result.h` not found.

- [ ] **Step 3: Implement `Result<T>`**

Write `c:/OpenADS/src/util/result.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace openads::util {

struct Error {
    std::int32_t code = 0;
    std::int32_t sub_code = 0;
    std::string  message;
    std::string  context;
};

namespace detail {
struct VoidTag {};
}

template <class T>
class Result {
public:
    using value_type = T;

    Result(T v) : data_(std::move(v)) {}
    Result(Error e) : data_(std::move(e)) {}

    bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    explicit operator bool() const noexcept { return has_value(); }

    T&        value() &       { return std::get<T>(data_); }
    const T&  value() const & { return std::get<T>(data_); }
    T&&       value() &&      { return std::move(std::get<T>(data_)); }

    Error&        error() &       { return std::get<Error>(data_); }
    const Error&  error() const & { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
    Result() : err_() {}
    Result(Error e) : err_(std::move(e)) {}

    bool has_value() const noexcept { return err_.code == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    const Error& error() const noexcept { return err_; }

private:
    Error err_;
};

} // namespace openads::util

// Try-macro: evaluates expr, returns its error from the enclosing
// function if it failed, otherwise binds the value to `decl`.
#define OPENADS_TRY(decl, expr)                          \
    auto _openads_try_##__LINE__ = (expr);               \
    if (!_openads_try_##__LINE__) {                      \
        return _openads_try_##__LINE__.error();          \
    }                                                    \
    decl = std::move(_openads_try_##__LINE__).value()
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 5 test cases pass.

- [ ] **Step 5: Commit**

```
git add src/util/result.h tests/unit/util_result_test.cpp
git commit -m "feat(util): Result<T> error-or-value type plus OPENADS_TRY"
```

---

## Task 4: `util/Span<T>` — non-owning view

**Files:**
- Create: `c:/OpenADS/src/util/span.h`
- Modify: `c:/OpenADS/tests/unit/util_span_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/util_span_test.cpp`:

```cpp
#include "doctest.h"
#include "util/span.h"

#include <array>
#include <cstdint>
#include <vector>

using openads::util::Span;

TEST_CASE("Span over std::vector exposes size and elements") {
    std::vector<int> v{1, 2, 3, 4};
    Span<int> s(v.data(), v.size());
    CHECK(s.size() == 4);
    CHECK(s[0] == 1);
    CHECK(s[3] == 4);
}

TEST_CASE("Span supports range-for") {
    std::array<std::uint8_t, 3> a{10, 20, 30};
    Span<std::uint8_t> s(a.data(), a.size());
    int total = 0;
    for (auto byte : s) total += byte;
    CHECK(total == 60);
}

TEST_CASE("Empty Span is empty") {
    Span<int> s(nullptr, 0);
    CHECK(s.empty());
    CHECK(s.size() == 0);
}

TEST_CASE("subspan returns a slice") {
    int data[] = {0, 1, 2, 3, 4};
    Span<int> s(data, 5);
    auto tail = s.subspan(2);
    CHECK(tail.size() == 3);
    CHECK(tail[0] == 2);
    auto mid = s.subspan(1, 3);
    CHECK(mid.size() == 3);
    CHECK(mid[2] == 3);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `util/span.h` not found.

- [ ] **Step 3: Implement `Span<T>`**

Write `c:/OpenADS/src/util/span.h`:

```cpp
#pragma once

#include <cstddef>
#include <cassert>

namespace openads::util {

template <class T>
class Span {
public:
    using element_type = T;
    using value_type   = std::remove_cv_t<T>;
    using size_type    = std::size_t;
    using pointer      = T*;
    using reference    = T&;
    using iterator     = T*;

    Span() noexcept = default;
    Span(T* data, size_type n) noexcept : data_(data), size_(n) {}

    pointer    data()  const noexcept { return data_; }
    size_type  size()  const noexcept { return size_; }
    bool       empty() const noexcept { return size_ == 0; }

    reference operator[](size_type i) const noexcept {
        assert(i < size_);
        return data_[i];
    }

    iterator begin() const noexcept { return data_; }
    iterator end()   const noexcept { return data_ + size_; }

    Span subspan(size_type offset) const noexcept {
        assert(offset <= size_);
        return Span(data_ + offset, size_ - offset);
    }
    Span subspan(size_type offset, size_type count) const noexcept {
        assert(offset + count <= size_);
        return Span(data_ + offset, count);
    }

private:
    T*        data_ = nullptr;
    size_type size_ = 0;
};

} // namespace openads::util
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 4 new test cases pass alongside the previous 5.

- [ ] **Step 5: Commit**

```
git add src/util/span.h tests/unit/util_span_test.cpp
git commit -m "feat(util): Span<T> non-owning view"
```

---

## Task 5: `util/Log` — structured logging sink

**Files:**
- Create: `c:/OpenADS/src/util/log.h`
- Modify: `c:/OpenADS/src/util/log.cpp`
- Modify: `c:/OpenADS/tests/unit/util_log_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/util_log_test.cpp`:

```cpp
#include "doctest.h"
#include "util/log.h"

#include <sstream>

using openads::util::Log;
using openads::util::LogLevel;

TEST_CASE("Log respects the configured level threshold") {
    std::ostringstream out;
    Log log{LogLevel::Info, &out};

    log.write(LogLevel::Debug, "debug-line");
    log.write(LogLevel::Info,  "info-line");
    log.write(LogLevel::Error, "err-line");

    const std::string buf = out.str();
    CHECK(buf.find("debug-line") == std::string::npos);
    CHECK(buf.find("info-line")  != std::string::npos);
    CHECK(buf.find("err-line")   != std::string::npos);
}

TEST_CASE("Log emits the level prefix") {
    std::ostringstream out;
    Log log{LogLevel::Trace, &out};
    log.write(LogLevel::Trace, "tag");
    CHECK(out.str().find("TRACE") != std::string::npos);
    CHECK(out.str().find("tag")   != std::string::npos);
}

TEST_CASE("Log discards output when sink is null") {
    Log log{LogLevel::Trace, nullptr};
    log.write(LogLevel::Error, "ignored");
    // No crash, no UB. Nothing else to assert.
    CHECK(true);
}

TEST_CASE("Log parses level from environment-style string") {
    CHECK(openads::util::log_level_from_string("trace") == LogLevel::Trace);
    CHECK(openads::util::log_level_from_string("DEBUG") == LogLevel::Debug);
    CHECK(openads::util::log_level_from_string("info")  == LogLevel::Info);
    CHECK(openads::util::log_level_from_string("warn")  == LogLevel::Warn);
    CHECK(openads::util::log_level_from_string("error") == LogLevel::Error);
    CHECK(openads::util::log_level_from_string("nonsense") == LogLevel::Info);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `util/log.h` not found.

- [ ] **Step 3: Implement `util/log.h`**

Write `c:/OpenADS/src/util/log.h`:

```cpp
#pragma once

#include <ostream>
#include <string_view>

namespace openads::util {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

class Log {
public:
    Log(LogLevel threshold, std::ostream* sink) noexcept
        : threshold_(threshold), sink_(sink) {}

    void write(LogLevel level, std::string_view message) noexcept;

    LogLevel threshold() const noexcept { return threshold_; }

private:
    LogLevel       threshold_;
    std::ostream*  sink_;
};

LogLevel log_level_from_string(std::string_view s) noexcept;

} // namespace openads::util
```

- [ ] **Step 4: Implement `util/log.cpp`**

Replace `c:/OpenADS/src/util/log.cpp`:

```cpp
#include "util/log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <string>

namespace openads::util {

namespace {

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string lower(std::string_view s) {
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

} // namespace

void Log::write(LogLevel level, std::string_view message) noexcept {
    if (sink_ == nullptr) return;
    if (static_cast<int>(level) < static_cast<int>(threshold_)) return;
    (*sink_) << level_name(level) << ' ' << message << '\n';
}

LogLevel log_level_from_string(std::string_view s) noexcept {
    const std::string norm = lower(s);
    if (norm == "trace") return LogLevel::Trace;
    if (norm == "debug") return LogLevel::Debug;
    if (norm == "info")  return LogLevel::Info;
    if (norm == "warn")  return LogLevel::Warn;
    if (norm == "error") return LogLevel::Error;
    return LogLevel::Info;
}

} // namespace openads::util
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 4 new test cases pass.

- [ ] **Step 6: Commit**

```
git add src/util/log.h src/util/log.cpp tests/unit/util_log_test.cpp
git commit -m "feat(util): leveled Log with stream sink and string parser"
```

---

## Task 6: `platform/File` — abstraction header and Win32 implementation

**Files:**
- Create: `c:/OpenADS/src/platform/file.h`
- Modify: `c:/OpenADS/src/platform/file_win32.cpp`
- Modify: `c:/OpenADS/src/platform/file_posix.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_file_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_file_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/file.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::platform::File;
using openads::platform::OpenMode;

namespace {

fs::path tmp_path(const char* tag) {
    return fs::temp_directory_path() / (std::string("openads_test_") + tag);
}

} // namespace

TEST_CASE("File: create, write, read, delete") {
    const auto p = tmp_path("file_basic");
    fs::remove(p);

    {
        auto opened = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(opened.has_value());
        File f = std::move(opened).value();

        const std::array<std::uint8_t, 5> payload{1, 2, 3, 4, 5};
        auto wrote = f.write_at(0, payload.data(), payload.size());
        REQUIRE(wrote.has_value());
        CHECK(wrote.value() == 5);
    }

    {
        auto opened = File::open(p.string(), OpenMode::ReadOnly);
        REQUIRE(opened.has_value());
        File f = std::move(opened).value();

        std::array<std::uint8_t, 5> buf{};
        auto got = f.read_at(0, buf.data(), buf.size());
        REQUIRE(got.has_value());
        CHECK(got.value() == 5);
        CHECK(buf[0] == 1);
        CHECK(buf[4] == 5);
    }

    fs::remove(p);
}

TEST_CASE("File: opening a missing file returns AE_FILE_NOT_FOUND-like error") {
    const auto p = tmp_path("file_missing");
    fs::remove(p);
    auto opened = File::open(p.string(), OpenMode::ReadOnly);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code != 0);
}

TEST_CASE("File: size grows with writes") {
    const auto p = tmp_path("file_size");
    fs::remove(p);
    auto opened = File::open(p.string(), OpenMode::CreateRW);
    REQUIRE(opened.has_value());
    File f = std::move(opened).value();

    std::array<std::uint8_t, 8> payload{0};
    REQUIRE(f.write_at(0,  payload.data(), payload.size()).has_value());
    REQUIRE(f.write_at(16, payload.data(), payload.size()).has_value());

    auto sz = f.size();
    REQUIRE(sz.has_value());
    CHECK(sz.value() == 24);

    fs::remove(p);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/file.h` not found.

- [ ] **Step 3: Implement `platform/file.h`**

Write `c:/OpenADS/src/platform/file.h`:

```cpp
#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openads::platform {

enum class OpenMode {
    ReadOnly,
    ReadWrite,
    CreateRW,    // create or truncate, read + write
    OpenExisting // read + write, fail if missing
};

class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    ~File();

    static util::Result<File> open(const std::string& path, OpenMode mode);

    util::Result<std::size_t> read_at (std::uint64_t offset,
                                       void* buf, std::size_t n);
    util::Result<std::size_t> write_at(std::uint64_t offset,
                                       const void* buf, std::size_t n);
    util::Result<std::uint64_t> size() const;
    util::Result<void> sync();

    // Native handle access for the lock + mmap layers below.
    void*    native_handle() const noexcept { return native_; }
    bool     is_open()       const noexcept { return native_ != nullptr; }

private:
    explicit File(void* native) noexcept : native_(native) {}
    void close_() noexcept;
    void* native_ = nullptr;
};

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/file_win32.cpp`**

Replace `c:/OpenADS/src/platform/file_win32.cpp`:

```cpp
#ifdef _WIN32

#include "platform/file.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    DWORD code = ::GetLastError();
    util::Error e;
    e.code = (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                 ? 5103   // AE_TABLE_NOT_FOUND-style placeholder
                 : 5000;  // AE_INTERNAL_ERROR placeholder
    e.sub_code = static_cast<std::int32_t>(code);
    e.message = op;
    return e;
}

} // namespace

File::File(File&& other) noexcept : native_(other.native_) {
    other.native_ = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close_();
        native_ = other.native_;
        other.native_ = nullptr;
    }
    return *this;
}

File::~File() { close_(); }

void File::close_() noexcept {
    if (native_ != nullptr) {
        ::CloseHandle(reinterpret_cast<HANDLE>(native_));
        native_ = nullptr;
    }
}

util::Result<File> File::open(const std::string& path, OpenMode mode) {
    DWORD access = 0;
    DWORD share  = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD disp   = OPEN_EXISTING;
    switch (mode) {
        case OpenMode::ReadOnly:
            access = GENERIC_READ;
            break;
        case OpenMode::ReadWrite:
            access = GENERIC_READ | GENERIC_WRITE;
            break;
        case OpenMode::CreateRW:
            access = GENERIC_READ | GENERIC_WRITE;
            disp   = CREATE_ALWAYS;
            break;
        case OpenMode::OpenExisting:
            access = GENERIC_READ | GENERIC_WRITE;
            disp   = OPEN_EXISTING;
            break;
    }
    HANDLE h = ::CreateFileA(path.c_str(), access, share, nullptr, disp,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return os_error("CreateFileA");
    return File{h};
}

util::Result<std::size_t> File::read_at(std::uint64_t offset,
                                        void* buf, std::size_t n) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    if (!::ReadFile(reinterpret_cast<HANDLE>(native_), buf,
                    static_cast<DWORD>(n), &got, &ov)) {
        DWORD code = ::GetLastError();
        if (code != ERROR_HANDLE_EOF) return os_error("ReadFile");
    }
    return static_cast<std::size_t>(got);
}

util::Result<std::size_t> File::write_at(std::uint64_t offset,
                                         const void* buf, std::size_t n) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD wrote = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(native_), buf,
                     static_cast<DWORD>(n), &wrote, &ov)) {
        return os_error("WriteFile");
    }
    return static_cast<std::size_t>(wrote);
}

util::Result<std::uint64_t> File::size() const {
    LARGE_INTEGER li{};
    if (!::GetFileSizeEx(reinterpret_cast<HANDLE>(native_), &li)) {
        return os_error("GetFileSizeEx");
    }
    return static_cast<std::uint64_t>(li.QuadPart);
}

util::Result<void> File::sync() {
    if (!::FlushFileBuffers(reinterpret_cast<HANDLE>(native_))) {
        return os_error("FlushFileBuffers");
    }
    return {};
}

} // namespace openads::platform

#endif // _WIN32
```

- [ ] **Step 5: Implement `platform/file_posix.cpp`**

Replace `c:/OpenADS/src/platform/file_posix.cpp`:

```cpp
#ifndef _WIN32

#include "platform/file.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == ENOENT) ? 5103 : 5000;
    e.sub_code = errno;
    e.message  = op;
    e.message += ": ";
    e.message += std::strerror(errno);
    return e;
}

intptr_t fd_from_native(void* p) {
    return reinterpret_cast<intptr_t>(p);
}
void* native_from_fd(int fd) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

} // namespace

File::File(File&& other) noexcept : native_(other.native_) {
    other.native_ = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close_();
        native_ = other.native_;
        other.native_ = nullptr;
    }
    return *this;
}

File::~File() { close_(); }

void File::close_() noexcept {
    if (native_ != nullptr) {
        ::close(static_cast<int>(fd_from_native(native_)));
        native_ = nullptr;
    }
}

util::Result<File> File::open(const std::string& path, OpenMode mode) {
    int flags = 0;
    switch (mode) {
        case OpenMode::ReadOnly:     flags = O_RDONLY; break;
        case OpenMode::ReadWrite:    flags = O_RDWR;   break;
        case OpenMode::CreateRW:     flags = O_RDWR | O_CREAT | O_TRUNC; break;
        case OpenMode::OpenExisting: flags = O_RDWR;   break;
    }
    int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0) return os_error("open");
    return File{native_from_fd(fd)};
}

util::Result<std::size_t> File::read_at(std::uint64_t offset,
                                        void* buf, std::size_t n) {
    int fd = static_cast<int>(fd_from_native(native_));
    ssize_t got = ::pread(fd, buf, n, static_cast<off_t>(offset));
    if (got < 0) return os_error("pread");
    return static_cast<std::size_t>(got);
}

util::Result<std::size_t> File::write_at(std::uint64_t offset,
                                         const void* buf, std::size_t n) {
    int fd = static_cast<int>(fd_from_native(native_));
    ssize_t wrote = ::pwrite(fd, buf, n, static_cast<off_t>(offset));
    if (wrote < 0) return os_error("pwrite");
    return static_cast<std::size_t>(wrote);
}

util::Result<std::uint64_t> File::size() const {
    int fd = static_cast<int>(fd_from_native(native_));
    struct stat st{};
    if (::fstat(fd, &st) != 0) return os_error("fstat");
    return static_cast<std::uint64_t>(st.st_size);
}

util::Result<void> File::sync() {
    int fd = static_cast<int>(fd_from_native(native_));
    if (::fsync(fd) != 0) return os_error("fsync");
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
```

- [ ] **Step 6: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 3 new test cases pass on the host platform.

- [ ] **Step 7: Commit**

```
git add src/platform/file.h src/platform/file_win32.cpp src/platform/file_posix.cpp tests/unit/platform_file_test.cpp
git commit -m "feat(platform): cross-platform File abstraction (Win32 + POSIX)"
```

---

## Task 7: `platform/Lock` — byte-range locking abstraction

**Files:**
- Create: `c:/OpenADS/src/platform/lock.h`
- Modify: `c:/OpenADS/src/platform/lock_win32.cpp`
- Modify: `c:/OpenADS/src/platform/lock_posix.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_lock_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_lock_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/file.h"
#include "platform/lock.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::platform::ByteLock;
using openads::platform::File;
using openads::platform::LockKind;
using openads::platform::OpenMode;

TEST_CASE("ByteLock acquires and releases an exclusive range") {
    const auto p = fs::temp_directory_path() / "openads_test_lock_excl";
    fs::remove(p);
    auto fres = File::open(p.string(), OpenMode::CreateRW);
    REQUIRE(fres.has_value());
    File f = std::move(fres).value();

    auto lock = ByteLock::acquire(f, 1000, 1, LockKind::Exclusive);
    REQUIRE(lock.has_value());
    // Releasing on scope exit; explicit release must also work:
    auto rel = std::move(lock).value().release();
    CHECK(rel.has_value());

    fs::remove(p);
}

TEST_CASE("ByteLock shared lock allows another shared lock") {
    const auto p = fs::temp_directory_path() / "openads_test_lock_shared";
    fs::remove(p);
    auto a = File::open(p.string(), OpenMode::CreateRW);
    REQUIRE(a.has_value());
    File fa = std::move(a).value();

    auto b = File::open(p.string(), OpenMode::OpenExisting);
    REQUIRE(b.has_value());
    File fb = std::move(b).value();

    auto la = ByteLock::acquire(fa, 5000, 1, LockKind::Shared);
    REQUIRE(la.has_value());
    auto lb = ByteLock::acquire(fb, 5000, 1, LockKind::Shared);
    REQUIRE(lb.has_value());

    fs::remove(p);
}

TEST_CASE("ByteLock exclusive lock blocks a second exclusive lock (try)") {
    const auto p = fs::temp_directory_path() / "openads_test_lock_block";
    fs::remove(p);
    auto a = File::open(p.string(), OpenMode::CreateRW);
    REQUIRE(a.has_value());
    File fa = std::move(a).value();

    auto b = File::open(p.string(), OpenMode::OpenExisting);
    REQUIRE(b.has_value());
    File fb = std::move(b).value();

    auto la = ByteLock::acquire(fa, 7000, 1, LockKind::Exclusive);
    REQUIRE(la.has_value());

    auto lb = ByteLock::try_acquire(fb, 7000, 1, LockKind::Exclusive);
    CHECK_FALSE(lb.has_value());

    fs::remove(p);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/lock.h` not found.

- [ ] **Step 3: Implement `platform/lock.h`**

Write `c:/OpenADS/src/platform/lock.h`:

```cpp
#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <cstdint>

namespace openads::platform {

enum class LockKind { Shared, Exclusive };

class ByteLock {
public:
    ByteLock() = default;
    ByteLock(const ByteLock&) = delete;
    ByteLock& operator=(const ByteLock&) = delete;
    ByteLock(ByteLock&&) noexcept;
    ByteLock& operator=(ByteLock&&) noexcept;
    ~ByteLock();

    static util::Result<ByteLock> acquire    (File& f, std::uint64_t offset,
                                              std::uint64_t length,
                                              LockKind kind);
    static util::Result<ByteLock> try_acquire(File& f, std::uint64_t offset,
                                              std::uint64_t length,
                                              LockKind kind);

    util::Result<void> release();

private:
    ByteLock(void* native, std::uint64_t off, std::uint64_t len) noexcept
        : native_(native), offset_(off), length_(len) {}

    void release_() noexcept;

    void*         native_ = nullptr;
    std::uint64_t offset_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/lock_win32.cpp`**

Replace `c:/OpenADS/src/platform/lock_win32.cpp`:

```cpp
#ifdef _WIN32

#include "platform/lock.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    DWORD code = ::GetLastError();
    util::Error e;
    e.code     = (code == ERROR_LOCK_VIOLATION) ? 5012 : 5013;
    e.sub_code = static_cast<std::int32_t>(code);
    e.message  = op;
    return e;
}

util::Result<ByteLock> do_lock(File& f, std::uint64_t offset,
                               std::uint64_t length, LockKind kind,
                               DWORD flags) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD lo      = static_cast<DWORD>(length & 0xFFFFFFFFu);
    DWORD hi      = static_cast<DWORD>(length >> 32);
    DWORD effective_flags = flags;
    if (kind == LockKind::Exclusive) effective_flags |= LOCKFILE_EXCLUSIVE_LOCK;
    HANDLE h = reinterpret_cast<HANDLE>(f.native_handle());
    if (!::LockFileEx(h, effective_flags, 0, lo, hi, &ov)) {
        return os_error("LockFileEx");
    }
    return ByteLock{f.native_handle(), offset, length};
}

} // namespace

ByteLock::ByteLock(ByteLock&& other) noexcept
    : native_(other.native_), offset_(other.offset_), length_(other.length_) {
    other.native_ = nullptr;
}

ByteLock& ByteLock::operator=(ByteLock&& other) noexcept {
    if (this != &other) {
        release_();
        native_ = other.native_;
        offset_ = other.offset_;
        length_ = other.length_;
        other.native_ = nullptr;
    }
    return *this;
}

ByteLock::~ByteLock() { release_(); }

void ByteLock::release_() noexcept {
    if (native_ == nullptr) return;
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset_ & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset_ >> 32);
    DWORD lo      = static_cast<DWORD>(length_ & 0xFFFFFFFFu);
    DWORD hi      = static_cast<DWORD>(length_ >> 32);
    ::UnlockFileEx(reinterpret_cast<HANDLE>(native_), 0, lo, hi, &ov);
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
    return do_lock(f, offset, length, kind, 0);
}

util::Result<ByteLock> ByteLock::try_acquire(File& f, std::uint64_t offset,
                                             std::uint64_t length,
                                             LockKind kind) {
    return do_lock(f, offset, length, kind, LOCKFILE_FAIL_IMMEDIATELY);
}

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // _WIN32
```

- [ ] **Step 5: Implement `platform/lock_posix.cpp`**

Replace `c:/OpenADS/src/platform/lock_posix.cpp`:

```cpp
#ifndef _WIN32

#include "platform/lock.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == EAGAIN || errno == EACCES) ? 5012 : 5013;
    e.sub_code = errno;
    e.message  = op;
    return e;
}

util::Result<ByteLock> do_lock(File& f, std::uint64_t offset,
                               std::uint64_t length, LockKind kind,
                               int cmd) {
    struct flock fl{};
    fl.l_type   = (kind == LockKind::Exclusive) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(offset);
    fl.l_len    = static_cast<off_t>(length);
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()));
    if (::fcntl(fd, cmd, &fl) == -1) return os_error("fcntl(F_SETLK)");
    return ByteLock{f.native_handle(), offset, length};
}

} // namespace

ByteLock::ByteLock(ByteLock&& other) noexcept
    : native_(other.native_), offset_(other.offset_), length_(other.length_) {
    other.native_ = nullptr;
}

ByteLock& ByteLock::operator=(ByteLock&& other) noexcept {
    if (this != &other) {
        release_();
        native_ = other.native_;
        offset_ = other.offset_;
        length_ = other.length_;
        other.native_ = nullptr;
    }
    return *this;
}

ByteLock::~ByteLock() { release_(); }

void ByteLock::release_() noexcept {
    if (native_ == nullptr) return;
    struct flock fl{};
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(offset_);
    fl.l_len    = static_cast<off_t>(length_);
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_));
    ::fcntl(fd, F_SETLK, &fl);
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
    return do_lock(f, offset, length, kind, F_SETLKW);
}

util::Result<ByteLock> ByteLock::try_acquire(File& f, std::uint64_t offset,
                                             std::uint64_t length,
                                             LockKind kind) {
    return do_lock(f, offset, length, kind, F_SETLK);
}

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
```

- [ ] **Step 6: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 3 new test cases pass.

- [ ] **Step 7: Commit**

```
git add src/platform/lock.h src/platform/lock_win32.cpp src/platform/lock_posix.cpp tests/unit/platform_lock_test.cpp
git commit -m "feat(platform): byte-range ByteLock abstraction (Win32 + POSIX)"
```

---

## Task 8: `platform/Mmap` — read-only memory map

**Files:**
- Create: `c:/OpenADS/src/platform/mmap.h`
- Modify: `c:/OpenADS/src/platform/mmap_win32.cpp`
- Modify: `c:/OpenADS/src/platform/mmap_posix.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_mmap_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_mmap_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/file.h"
#include "platform/mmap.h"

#include <array>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;
using openads::platform::File;
using openads::platform::FileMap;
using openads::platform::OpenMode;

TEST_CASE("FileMap exposes a read-only view of the file") {
    const auto p = fs::temp_directory_path() / "openads_test_mmap";
    fs::remove(p);
    {
        auto fres = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();
        std::array<std::uint8_t, 8> payload{0xDE, 0xAD, 0xBE, 0xEF,
                                            0x01, 0x02, 0x03, 0x04};
        REQUIRE(f.write_at(0, payload.data(), payload.size()).has_value());
    }

    auto fres = File::open(p.string(), OpenMode::ReadOnly);
    REQUIRE(fres.has_value());
    File f = std::move(fres).value();

    auto m = FileMap::map_readonly(f, 0, 8);
    REQUIRE(m.has_value());
    auto bytes = std::move(m).value().bytes();
    CHECK(bytes.size() == 8);
    CHECK(bytes[0] == 0xDE);
    CHECK(bytes[3] == 0xEF);

    fs::remove(p);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/mmap.h` not found.

- [ ] **Step 3: Implement `platform/mmap.h`**

Write `c:/OpenADS/src/platform/mmap.h`:

```cpp
#pragma once

#include "platform/file.h"
#include "util/result.h"
#include "util/span.h"

#include <cstdint>

namespace openads::platform {

class FileMap {
public:
    FileMap() = default;
    FileMap(const FileMap&) = delete;
    FileMap& operator=(const FileMap&) = delete;
    FileMap(FileMap&&) noexcept;
    FileMap& operator=(FileMap&&) noexcept;
    ~FileMap();

    static util::Result<FileMap> map_readonly(File& f, std::uint64_t offset,
                                              std::size_t length);

    util::Span<const std::uint8_t> bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(view_), length_};
    }

private:
    FileMap(void* mapping, void* view, std::size_t length) noexcept
        : mapping_(mapping), view_(view), length_(length) {}

    void unmap_() noexcept;

    void*       mapping_ = nullptr; // Win32: HANDLE; POSIX: unused
    void*       view_    = nullptr;
    std::size_t length_  = 0;
};

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/mmap_win32.cpp`**

Replace `c:/OpenADS/src/platform/mmap_win32.cpp`:

```cpp
#ifdef _WIN32

#include "platform/mmap.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = 5000;
    e.sub_code = static_cast<std::int32_t>(::GetLastError());
    e.message  = op;
    return e;
}

} // namespace

FileMap::FileMap(FileMap&& other) noexcept
    : mapping_(other.mapping_), view_(other.view_), length_(other.length_) {
    other.mapping_ = nullptr;
    other.view_    = nullptr;
    other.length_  = 0;
}

FileMap& FileMap::operator=(FileMap&& other) noexcept {
    if (this != &other) {
        unmap_();
        mapping_ = other.mapping_;
        view_    = other.view_;
        length_  = other.length_;
        other.mapping_ = nullptr;
        other.view_    = nullptr;
        other.length_  = 0;
    }
    return *this;
}

FileMap::~FileMap() { unmap_(); }

void FileMap::unmap_() noexcept {
    if (view_) {
        ::UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        ::CloseHandle(reinterpret_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
    length_ = 0;
}

util::Result<FileMap> FileMap::map_readonly(File& f, std::uint64_t offset,
                                            std::size_t length) {
    HANDLE h = reinterpret_cast<HANDLE>(f.native_handle());
    HANDLE m = ::CreateFileMappingA(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) return os_error("CreateFileMappingA");
    DWORD lo = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    DWORD hi = static_cast<DWORD>(offset >> 32);
    void* v = ::MapViewOfFile(m, FILE_MAP_READ, hi, lo, length);
    if (!v) { ::CloseHandle(m); return os_error("MapViewOfFile"); }
    return FileMap{m, v, length};
}

} // namespace openads::platform

#endif // _WIN32
```

- [ ] **Step 5: Implement `platform/mmap_posix.cpp`**

Replace `c:/OpenADS/src/platform/mmap_posix.cpp`:

```cpp
#ifndef _WIN32

#include "platform/mmap.h"

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = 5000;
    e.sub_code = errno;
    e.message  = op;
    return e;
}

} // namespace

FileMap::FileMap(FileMap&& other) noexcept
    : mapping_(other.mapping_), view_(other.view_), length_(other.length_) {
    other.mapping_ = nullptr;
    other.view_    = nullptr;
    other.length_  = 0;
}

FileMap& FileMap::operator=(FileMap&& other) noexcept {
    if (this != &other) {
        unmap_();
        mapping_ = other.mapping_;
        view_    = other.view_;
        length_  = other.length_;
        other.mapping_ = nullptr;
        other.view_    = nullptr;
        other.length_  = 0;
    }
    return *this;
}

FileMap::~FileMap() { unmap_(); }

void FileMap::unmap_() noexcept {
    if (view_) {
        ::munmap(view_, length_);
        view_ = nullptr;
    }
    length_ = 0;
}

util::Result<FileMap> FileMap::map_readonly(File& f, std::uint64_t offset,
                                            std::size_t length) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()));
    void* v = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd,
                     static_cast<off_t>(offset));
    if (v == MAP_FAILED) return os_error("mmap");
    return FileMap{nullptr, v, length};
}

} // namespace openads::platform

#endif // !_WIN32
```

- [ ] **Step 6: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: the new test case passes.

- [ ] **Step 7: Commit**

```
git add src/platform/mmap.h src/platform/mmap_win32.cpp src/platform/mmap_posix.cpp tests/unit/platform_mmap_test.cpp
git commit -m "feat(platform): read-only FileMap (Win32 + POSIX)"
```

---

## Task 9: `platform/path` — case-insensitive lookup helper

**Files:**
- Create: `c:/OpenADS/src/platform/path.h`
- Modify: `c:/OpenADS/src/platform/path.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_path_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_path_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/path.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using openads::platform::resolve_case_insensitive;

TEST_CASE("Case-insensitive resolve returns existing path verbatim") {
    const auto dir = fs::temp_directory_path() / "openads_path_t1";
    fs::create_directories(dir);
    const auto file = dir / "Clientes.dbf";
    { std::ofstream(file) << "x"; }

    auto resolved = resolve_case_insensitive((dir / "Clientes.dbf").string());
    CHECK(resolved == file.string());

    fs::remove_all(dir);
}

TEST_CASE("Case-insensitive resolve matches by case-folded leaf") {
    const auto dir = fs::temp_directory_path() / "openads_path_t2";
    fs::create_directories(dir);
    const auto file = dir / "Clientes.DBF";
    { std::ofstream(file) << "x"; }

    auto resolved = resolve_case_insensitive((dir / "clientes.dbf").string());
    CHECK(resolved == file.string());

    fs::remove_all(dir);
}

TEST_CASE("Case-insensitive resolve returns input on miss") {
    const auto dir = fs::temp_directory_path() / "openads_path_t3";
    fs::create_directories(dir);
    const auto missing = (dir / "Nope.dbf").string();

    auto resolved = resolve_case_insensitive(missing);
    CHECK(resolved == missing);

    fs::remove_all(dir);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/path.h` not found.

- [ ] **Step 3: Implement `platform/path.h`**

Write `c:/OpenADS/src/platform/path.h`:

```cpp
#pragma once

#include <string>

namespace openads::platform {

// On Windows the filesystem is already case-insensitive; on POSIX this
// scans the parent directory once to find a case-folded match. Returns
// the input unchanged if no match exists.
std::string resolve_case_insensitive(const std::string& path);

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/path.cpp`**

Replace `c:/OpenADS/src/platform/path.cpp`:

```cpp
#include "platform/path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace openads::platform {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

std::string resolve_case_insensitive(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return path;

    fs::path p(path);
    fs::path parent = p.parent_path();
    if (parent.empty()) parent = ".";
    if (!fs::is_directory(parent, ec)) return path;

    const std::string leaf_lower = to_lower(p.filename().string());
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        if (ec) break;
        if (to_lower(entry.path().filename().string()) == leaf_lower) {
            return entry.path().string();
        }
    }
    return path;
}

} // namespace openads::platform
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 3 new test cases pass.

- [ ] **Step 6: Commit**

```
git add src/platform/path.h src/platform/path.cpp tests/unit/platform_path_test.cpp
git commit -m "feat(platform): case-insensitive path resolver for POSIX hosts"
```

---

## Task 10: `platform/time` — monotonic + UTC clock wrappers

**Files:**
- Create: `c:/OpenADS/src/platform/time.h`
- Modify: `c:/OpenADS/src/platform/time.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_time_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_time_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/time.h"

#include <thread>
#include <chrono>

using openads::platform::monotonic_nanos;
using openads::platform::utc_unix_micros;

TEST_CASE("monotonic_nanos is non-decreasing across two reads") {
    auto a = monotonic_nanos();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto b = monotonic_nanos();
    CHECK(b >= a);
}

TEST_CASE("utc_unix_micros falls within a sane range") {
    auto t = utc_unix_micros();
    // 2024-01-01 .. 2100-01-01 in microseconds.
    CHECK(t > 1'700'000'000'000'000ll);
    CHECK(t < 4'102'444'800'000'000ll);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/time.h` not found.

- [ ] **Step 3: Implement `platform/time.h`**

Write `c:/OpenADS/src/platform/time.h`:

```cpp
#pragma once

#include <cstdint>

namespace openads::platform {

std::int64_t monotonic_nanos();   // monotonic, never decreases
std::int64_t utc_unix_micros();   // microseconds since 1970-01-01 UTC

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/time.cpp`**

Replace `c:/OpenADS/src/platform/time.cpp`:

```cpp
#include "platform/time.h"

#include <chrono>

namespace openads::platform {

std::int64_t monotonic_nanos() {
    using clock = std::chrono::steady_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

std::int64_t utc_unix_micros() {
    using clock = std::chrono::system_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

} // namespace openads::platform
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 2 new test cases pass.

- [ ] **Step 6: Commit**

```
git add src/platform/time.h src/platform/time.cpp tests/unit/platform_time_test.cpp
git commit -m "feat(platform): monotonic and UTC clock wrappers"
```

---

## Task 11: `platform/thread` — current thread id

**Files:**
- Create: `c:/OpenADS/src/platform/thread.h`
- Modify: `c:/OpenADS/src/platform/thread.cpp`
- Modify: `c:/OpenADS/tests/unit/platform_thread_test.cpp`

- [ ] **Step 1: Write the failing tests**

Replace `c:/OpenADS/tests/unit/platform_thread_test.cpp`:

```cpp
#include "doctest.h"
#include "platform/thread.h"

#include <thread>

using openads::platform::current_thread_id;

TEST_CASE("current_thread_id is non-zero for the main thread") {
    auto id = current_thread_id();
    CHECK(id != 0);
}

TEST_CASE("current_thread_id differs across threads") {
    auto a = current_thread_id();
    std::uint64_t b = 0;
    std::thread t([&] { b = current_thread_id(); });
    t.join();
    CHECK(b != 0);
    CHECK(b != a);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:

```
cmake --build build/default --target openads_unit_tests
```

Expected: compile error, `platform/thread.h` not found.

- [ ] **Step 3: Implement `platform/thread.h`**

Write `c:/OpenADS/src/platform/thread.h`:

```cpp
#pragma once

#include <cstdint>

namespace openads::platform {

std::uint64_t current_thread_id();

} // namespace openads::platform
```

- [ ] **Step 4: Implement `platform/thread.cpp`**

Replace `c:/OpenADS/src/platform/thread.cpp`:

```cpp
#include "platform/thread.h"

#include <thread>
#include <functional>

namespace openads::platform {

std::uint64_t current_thread_id() {
    return static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

} // namespace openads::platform
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```
cmake --build build/default --target openads_unit_tests
ctest --preset default --output-on-failure
```

Expected: 2 new test cases pass.

- [ ] **Step 6: Commit**

```
git add src/platform/thread.h src/platform/thread.cpp tests/unit/platform_thread_test.cpp
git commit -m "feat(platform): current_thread_id helper"
```

---

## Task 12: Public-header placeholders under `include/openads/`

**Files:**
- Create: `c:/OpenADS/include/openads/version.h`
- Create: `c:/OpenADS/include/openads/error.h`
- Create: `c:/OpenADS/include/openads/platform.h`

- [ ] **Step 1: Write `version.h`**

```cpp
#pragma once

#define OPENADS_VERSION_MAJOR 0
#define OPENADS_VERSION_MINOR 0
#define OPENADS_VERSION_PATCH 1
#define OPENADS_VERSION_STRING "0.0.1"
```

- [ ] **Step 2: Write `error.h`**

```cpp
#pragma once

#include <cstdint>

namespace openads {

// Mirror of the ACE error code surface OpenADS will emit.
// See README "Error handling" section for the full table.
enum : std::uint32_t {
    AE_SUCCESS                  = 0,
    AE_INTERNAL_ERROR           = 5000,
    AE_FUNCTION_NOT_AVAILABLE   = 5004,
    AE_LOCKED                   = 5012,
    AE_LOCK_FAILED              = 5013,
    AE_NO_CONNECTION            = 5036,
    AE_COLUMN_NOT_FOUND         = 5063,
    AE_TABLE_NOT_FOUND          = 5066,
    AE_TABLE_CORRUPTED          = 5103,
    AE_INVALID_CONNECTION_HANDLE = 4097,
    AE_PARSE_ERROR              = 7200,
    AE_INVALID_SQL_TOKEN        = 7201,
    AE_TYPE_MISMATCH            = 7041,
    AE_DIVISION_BY_ZERO         = 7042
};

} // namespace openads
```

- [ ] **Step 3: Write `platform.h`**

```cpp
#pragma once

// Empty for M0. Exists so that downstream milestones can include
// <openads/platform.h> without breaking the build before the L1 ABI
// surface lands.
```

- [ ] **Step 4: Build to verify the new headers compile**

Run:

```
cmake --build build/default
```

Expected: success.

- [ ] **Step 5: Commit**

```
git add include/openads/version.h include/openads/error.h include/openads/platform.h
git commit -m "feat(api): public header placeholders for version, error, platform"
```

---

## Task 13: GitHub Actions CI

**Files:**
- Create: `c:/OpenADS/.github/workflows/ci.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: ci

on:
  push:
    branches: [main]
  pull_request:

jobs:
  build-and-test:
    name: ${{ matrix.os }} / ${{ matrix.preset }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: windows-2022
            preset: msvc-x64
          - os: ubuntu-22.04
            preset: ninja-clang
          - os: macos-13
            preset: default
    steps:
      - uses: actions/checkout@v4

      - name: Install Ninja (Linux / macOS)
        if: runner.os != 'Windows'
        uses: seanmiddleditch/gha-setup-ninja@v4

      - name: Configure
        run: cmake --preset ${{ matrix.preset }}

      - name: Build
        run: cmake --build build/${{ matrix.preset }} --config Release

      - name: Test
        run: ctest --test-dir build/${{ matrix.preset }} --output-on-failure -C Release
```

- [ ] **Step 2: Commit**

```
git add .github/workflows/ci.yml
git commit -m "ci: add GitHub Actions matrix for Windows / Linux / macOS"
```

- [ ] **Step 3: Push and verify the matrix is green**

```
git push origin main
```

Open the Actions tab on the GitHub repository (`FiveTechSoft/OpenADS`) and confirm all three matrix legs (`windows-2022 / msvc-x64`, `ubuntu-22.04 / ninja-clang`, `macos-13 / default`) finish green.

If any leg fails, fix the underlying portability issue inline (most likely a `_WIN32` guard or a header include) and push a follow-up commit until the matrix is green.

---

## Task 14: Update `README.md` build instructions

**Files:**
- Modify: `c:/OpenADS/README.md`

- [ ] **Step 1: Append a Build section before `## License`**

Insert the following block immediately above the existing `## License` heading:

```markdown
## Build (M0 skeleton)

```
git clone https://github.com/FiveTechSoft/OpenADS.git
cd OpenADS
cmake --preset default
cmake --build build/default
ctest --preset default --output-on-failure
```

Other presets: `debug`, `msvc-x64`, `ninja-clang` — see `CMakePresets.json`.
```

- [ ] **Step 2: Commit**

```
git add README.md
git commit -m "docs: M0 build instructions"
```

- [ ] **Step 3: Push**

```
git push origin main
```

Confirm CI is still green after this commit.

---

## Done

At the end of M0:

- `cmake --preset default && cmake --build build/default && ctest --preset default` succeeds on Windows, Linux, and macOS.
- ~26 unit tests across `util/` and `platform/` pass.
- `openads_core.lib` / `libopenads_core.a` builds against only the C++ standard library and platform basics — no third-party runtime dependencies.
- The repository skeleton matches the layout in the README, ready for M1 to land DBF read.
