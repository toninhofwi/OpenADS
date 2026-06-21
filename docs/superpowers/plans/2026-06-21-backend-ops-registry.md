# Backend-ops registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the per-backend `#if OPENADS_WITH_X` table dispatch out of the ~17 ABI functions in `ace_exports.cpp` into a single `HandleKind`-keyed ops registry, proven by migrating SQLite with the unit suite staying green.

**Architecture:** A `BackendTableOps` vtable (function pointers mirroring the ABI table ops) is registered per `HandleKind`. Each ABI function does one `backend_table_ops_for(h)` lookup and dispatches if non-null, else runs the unchanged native/remote (`tcp://`) path. SQLite's inline dispatch bodies are lifted into named static `sqlite_<op>()` functions and exposed as one `BackendTableOps` instance.

**Tech Stack:** C++17, MSVC `cl` x64, CMake + Ninja, doctest unit suite.

## Refinement vs spec (flagged)

The spec (`docs/superpowers/specs/2026-06-21-backend-ops-registry-design.md`) placed the lifted `sqlite_<op>()` functions "in the backend file". During planning, the inline SQLite bodies were found to depend on `ace_exports.cpp`-local helpers (`fail`, `ok`, `get_sqlite_table`, `sqlite_field_index`, `pad_char_field`). For this POC the lifted functions and the `sqlite_table_ops()` accessor therefore live in **a single `#if defined(OPENADS_WITH_SQLITE)` section inside `ace_exports.cpp`**, reusing those helpers. This still removes the per-function `#if` (the merge-conflict surface): the 17 ABI functions become backend-agnostic, and each backend's PR touches only its own ops section + one registration line. Fully relocating the ops into `sql_backend/*.cpp` (exposing the helpers via a header) is a clean follow-up once the mechanism is green. `backend_table_ops.h` and `backend_registry.{h,cpp}` are backend-agnostic and reusable as the spec intends.

## Global Constraints

- Behavior-preserving refactor: **no edits to any test file**; the existing
  `openads_unit_tests` suite is the regression guard.
- Native local DBF/ADT and native client/server (`tcp://`, `RemoteTable`,
  `HandleKind::Table`/`RemoteTable`) MUST remain the unchanged fall-through. They
  do NOT register ops.
- No change to the public ACE ABI (exported function signatures unchanged).
- Build/test env: run from a shell with MSVC x64 on PATH
  (`_UtlAI\msvc\setup_x64.bat`); configure with `OPENADS_WITH_SQLITE=ON`.
- Branch: `refactor/backend-ops-registry` (base `973d6f3`). Supervised merge to
  the integration line per regra 7.
- Each commit ends with the `Co-Authored-By: Claude Opus 4.8 (1M context)` trailer.

---

## Baseline build & test commands (used by every task)

Configure (once; reuse the dir afterward):
```
cmake -S . -B build\reg-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DOPENADS_WITH_SQLITE=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WITH_TLS=OFF ^
  -DOPENADS_WARNINGS_AS_ERRORS=OFF
```
Build + run suite:
```
cmake --build build\reg-msvc
build\reg-msvc\tests\openads_unit_tests.exe
```
Expected suite result throughout: `0 failed` (baseline 528 passed / 2 skipped on
the equivalent ODBC tree; the exact pass count on this SQLite config is recorded
in Task 0 and must not regress).

---

### Task 0: Pin the green baseline

**Files:** none (measurement only).

- [ ] **Step 1: Configure**

Run the configure command above. Expected: `-- Configuring done` / `-- Generating done`, SQLite backend detected.

- [ ] **Step 2: Build**

Run: `cmake --build build\reg-msvc`
Expected: builds `openace64.dll` and `tests\openads_unit_tests.exe`, 0 errors.

- [ ] **Step 3: Run suite and record the number**

Run: `build\reg-msvc\tests\openads_unit_tests.exe`
Expected: `Status: SUCCESS!`, `0 failed`. Write the exact `test cases: N | N passed`
line into the PR/commit notes — this is the number every later task must match.

- [ ] **Step 4: Commit the plan/baseline note** (no code yet)

```
git add docs/superpowers/plans/2026-06-21-backend-ops-registry.md
git commit -m "docs(plan): backend-ops registry implementation plan"
```

---

### Task 1: `HandleKind kind_of(Handle)` on the registry

Lets `backend_table_ops_for` find a handle's kind without knowing the backend type.

**Files:**
- Modify: `src/session/handle_registry.h` (add method to `HandleRegistry`)
- Test: `tests/unit/handle_registry_kind_of_test.cpp` (new)
- Modify: `tests/CMakeLists.txt` (register the new test source)

**Interfaces:**
- Produces: `openads::session::HandleKind HandleRegistry::kind_of(Handle h) const;`
  — returns the slot's kind, or `HandleKind::None` if `h` is unknown.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/handle_registry_kind_of_test.cpp
#include "doctest.h"
#include "session/handle_registry.h"

using openads::session::HandleRegistry;
using openads::session::HandleKind;

TEST_CASE("kind_of returns the registered kind, None for unknown") {
    HandleRegistry reg;
    int dummy = 0;
    auto h = reg.register_object(HandleKind::SqliteTable, &dummy);
    CHECK(reg.kind_of(h) == HandleKind::SqliteTable);
    CHECK(reg.kind_of(h + 999) == HandleKind::None);
    reg.release(h);
    CHECK(reg.kind_of(h) == HandleKind::None);
}
```

- [ ] **Step 2: Add the test to the build**

In `tests/CMakeLists.txt`, add `unit/handle_registry_kind_of_test.cpp` to the
`openads_unit_tests` source list (follow the existing pattern of the adjacent
`unit/*_test.cpp` entries).

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build\reg-msvc && build\reg-msvc\tests\openads_unit_tests.exe -tc="kind_of*"`
Expected: FAIL to compile — `kind_of` not a member of `HandleRegistry`.

- [ ] **Step 4: Implement `kind_of`**

In `src/session/handle_registry.h`, inside `class HandleRegistry`, after `for_each_handle`:
```cpp
    HandleKind kind_of(Handle h) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = slots_.find(h);
        return it == slots_.end() ? HandleKind::None : it->second.kind;
    }
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build\reg-msvc && build\reg-msvc\tests\openads_unit_tests.exe -tc="kind_of*"`
Expected: PASS.

- [ ] **Step 6: Full suite still green**

Run: `build\reg-msvc\tests\openads_unit_tests.exe`
Expected: `0 failed`, same total as Task 0 + 1 new case.

- [ ] **Step 7: Commit**

```
git add src/session/handle_registry.h tests/unit/handle_registry_kind_of_test.cpp tests/CMakeLists.txt
git commit -m "feat(session): HandleRegistry::kind_of for backend dispatch"
```

---

### Task 2: Backend-ops vtable + registry

**Files:**
- Create: `src/abi/backend_table_ops.h`
- Create: `src/abi/backend_registry.h`
- Create: `src/abi/backend_registry.cpp`
- Modify: `src/CMakeLists.txt` (add `abi/backend_registry.cpp` to `openads_core` sources)
- Test: `tests/unit/backend_registry_test.cpp` (new) + `tests/CMakeLists.txt`

**Interfaces:**
- Produces (`backend_table_ops.h`):
  ```cpp
  namespace openads::abi {
  struct BackendTableOps {
      UNSIGNED32 (*close_table)      (ADSHANDLE);
      UNSIGNED32 (*goto_top)         (ADSHANDLE);
      UNSIGNED32 (*goto_bottom)      (ADSHANDLE);
      UNSIGNED32 (*skip)             (ADSHANDLE, SIGNED32);
      UNSIGNED32 (*at_eof)           (ADSHANDLE, UNSIGNED16*);
      UNSIGNED32 (*at_bof)           (ADSHANDLE, UNSIGNED16*);
      UNSIGNED32 (*num_fields)       (ADSHANDLE, UNSIGNED16*);
      UNSIGNED32 (*field_name)       (ADSHANDLE, UNSIGNED16, UNSIGNED8*, UNSIGNED16*);
      UNSIGNED32 (*field_type)       (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
      UNSIGNED32 (*field_length)     (ADSHANDLE, UNSIGNED8*, UNSIGNED32*);
      UNSIGNED32 (*field_decimals)   (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
      UNSIGNED32 (*record_num)       (ADSHANDLE, UNSIGNED32*);
      UNSIGNED32 (*record_count)     (ADSHANDLE, UNSIGNED32*, UNSIGNED16);
      UNSIGNED32 (*get_field)        (ADSHANDLE, UNSIGNED8*, UNSIGNED8*, UNSIGNED32*, UNSIGNED16);
      UNSIGNED32 (*is_record_deleted)(ADSHANDLE, UNSIGNED16*);
      UNSIGNED32 (*open_index)       (ADSHANDLE, UNSIGNED8*, ADSHANDLE*, UNSIGNED16*);
      UNSIGNED32 (*is_found)         (ADSHANDLE, UNSIGNED16*);
  };
  } // namespace openads::abi
  ```
  (The exact `ACE.h` typedefs `ADSHANDLE/UNSIGNED32/SIGNED32/UNSIGNED16/UNSIGNED8`
  are reached by `#include "ace.h"`; copy the include path used at the top of
  `ace_exports.cpp`.)
- Produces (`backend_registry.h`):
  ```cpp
  namespace openads::abi {
  void register_backend_table_ops(openads::session::HandleKind kind,
                                  const BackendTableOps* ops);
  const BackendTableOps* backend_table_ops_for(ADSHANDLE h);
  void register_builtin_backends();   // defined in ace_exports.cpp
  }
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/backend_registry_test.cpp
#include "doctest.h"
#include "abi/backend_table_ops.h"
#include "abi/backend_registry.h"
#include "session/handle_registry.h"

using namespace openads::abi;
using openads::session::HandleKind;

static UNSIGNED32 fake_goto_top(ADSHANDLE) { return 4242; }

TEST_CASE("registry returns ops for a registered kind, null otherwise") {
    static const BackendTableOps ops = [] {
        BackendTableOps o{};
        o.goto_top = &fake_goto_top;
        return o;
    }();
    register_backend_table_ops(HandleKind::SqliteTable, &ops);
    // Native kinds never registered -> a Table handle resolves to null.
    // (Uses the process-wide session registry; see note below.)
    CHECK(backend_table_ops_for(/*unknown handle*/ 0) == nullptr);
}
```

Note: `backend_table_ops_for` reads the process `state().registry`. The test
above only asserts the null path for an unknown handle (kind `None`); the
positive end-to-end path is covered by the existing suite once SQLite is wired
(Task 4). Keep this unit test minimal and dependency-free.

- [ ] **Step 2: Add test to `tests/CMakeLists.txt`** (same pattern as Task 1).

- [ ] **Step 3: Create `src/abi/backend_table_ops.h`** with the struct above.

- [ ] **Step 4: Create `src/abi/backend_registry.h`** with the declarations above.

- [ ] **Step 5: Create `src/abi/backend_registry.cpp`**

```cpp
#include "abi/backend_registry.h"
#include "abi/backend_table_ops.h"
#include "session/handle_registry.h"
#include <array>

namespace openads::abi {

namespace {
// Small fixed table indexed by HandleKind (enum is dense, max value 11).
std::array<const BackendTableOps*, 32>& ops_table() {
    static std::array<const BackendTableOps*, 32> t{};  // all nullptr
    return t;
}
}  // namespace

void register_backend_table_ops(openads::session::HandleKind kind,
                                const BackendTableOps* ops) {
    auto idx = static_cast<std::size_t>(kind);
    if (idx < ops_table().size()) ops_table()[idx] = ops;
}

const BackendTableOps* ops_for_kind(openads::session::HandleKind kind) {
    auto idx = static_cast<std::size_t>(kind);
    return idx < ops_table().size() ? ops_table()[idx] : nullptr;
}

}  // namespace openads::abi
```

The `backend_table_ops_for(ADSHANDLE)` definition lives in `ace_exports.cpp`
(Task 3) because it needs `state()`; it calls `ops_for_kind`. Declare
`ops_for_kind` in `backend_registry.h` too.

- [ ] **Step 6: Build + run new test fails-then-passes**

Run: `cmake --build build\reg-msvc && build\reg-msvc\tests\openads_unit_tests.exe -tc="registry returns*"`
Expected: PASS (null path). If link error on `backend_table_ops_for`, it is
defined in Task 3 — temporarily stub it in the test or order Task 3 first; see
Step 7.

- [ ] **Step 7: Ordering note**

`backend_table_ops_for` is defined in Task 3. If building Task 2 alone fails to
link, do Tasks 2 and 3 as one commit (they form one compilable unit). Prefer
keeping the registry test asserting only `ops_for_kind` if `backend_table_ops_for`
is not yet linkable:
```cpp
CHECK(ops_for_kind(HandleKind::Table) == nullptr);
CHECK(ops_for_kind(HandleKind::SqliteTable) == &ops);
```

- [ ] **Step 8: Commit**

```
git add src/abi/backend_table_ops.h src/abi/backend_registry.h src/abi/backend_registry.cpp src/CMakeLists.txt tests/unit/backend_registry_test.cpp tests/CMakeLists.txt
git commit -m "feat(abi): backend-ops vtable + HandleKind-keyed registry"
```

---

### Task 3: SQLite ops section + registration + dispatcher

Add the lifted SQLite ops and the once-init dispatcher. The 17 ABI functions are
NOT touched yet — so behavior is unchanged and the suite stays green.

**Files:**
- Modify: `src/abi/ace_exports.cpp` (add a new `#if defined(OPENADS_WITH_SQLITE)`
  ops section near the existing SQLite helpers ~line 341; add
  `backend_table_ops_for` + `register_builtin_backends` in the anonymous namespace)

**Interfaces:**
- Consumes: `register_backend_table_ops`, `ops_for_kind` (Task 2),
  `state()`, `get_sqlite_table`, `sqlite_field_index`, `fail`, `ok`,
  `pad_char_field`, `openads::abi::to_internal`, `openads::abi::copy_to_caller`
  (all existing in `ace_exports.cpp`).
- Produces: `const BackendTableOps* backend_table_ops_for(ADSHANDLE)` and a
  registered `SqliteTable` ops instance.

- [ ] **Step 1: Add the dispatcher + once-init** (anonymous namespace, after the
  includes of `backend_registry.h`/`backend_table_ops.h`)

```cpp
const openads::abi::BackendTableOps* backend_table_ops_for(ADSHANDLE h) {
    static const bool _ = (openads::abi::register_builtin_backends(), true);
    (void)_;
    return openads::abi::ops_for_kind(state().registry.kind_of(h));
}
```

- [ ] **Step 2: Add the lifted SQLite ops** in a single
  `#if defined(OPENADS_WITH_SQLITE)` block. Each function is the body currently
  inline in the matching ABI function, with `hTable` as its `ADSHANDLE` param.
  Full text for the three body-shapes; the rest are identical-shaped per the
  table after.

Nav shape (goto_top/goto_bottom/skip):
```cpp
UNSIGNED32 sqlite_goto_top(ADSHANDLE hTable) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}
UNSIGNED32 sqlite_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}
UNSIGNED32 sqlite_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}
```

get_field shape (the value-reading body from lines 2755-2773):
```cpp
UNSIGNED32 sqlite_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                            UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                            UNSIGNED16 /*usOption*/) {
    auto* st = get_sqlite_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = sqlite_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}
```

Metadata/predicate shape (at_eof shown; at_bof, num_fields, field_name,
field_type, field_length, field_decimals, record_num, record_count,
is_record_deleted, open_index, is_found follow the same lift — copy each one's
existing inline SQLite body verbatim, parameter-for-parameter):
```cpp
UNSIGNED32 sqlite_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_sqlite_table(hTable);
    /* <copy the body currently inside the `if (auto* st = get_sqlite_table(hTable))`
       block of AdsAtEOF, lines ~2191-2215, replacing the opening
       `if (auto* st = ...) {` with the `auto* st = get_sqlite_table(hTable);`
       line above and removing the closing brace> */
}
```

The 17 functions and their lifted names (each body copied verbatim from the
named ABI function's current inline SQLite block):

| Lifted fn | From ABI fn | Source lines |
|---|---|---|
| `sqlite_close_table`      | `AdsCloseTable`       | ~2050 |
| `sqlite_goto_top`         | `AdsGotoTop`          | 2102-2109 |
| `sqlite_goto_bottom`      | `AdsGotoBottom`       | 2126-2133 |
| `sqlite_skip`             | `AdsSkip`             | 2165-2172 |
| `sqlite_at_eof`           | `AdsAtEOF`            | ~2191 |
| `sqlite_at_bof`           | `AdsAtBOF`            | ~2217 |
| `sqlite_num_fields`       | `AdsGetNumFields`     | ~2246 |
| `sqlite_field_name`       | `AdsGetFieldName`     | ~2285 |
| `sqlite_field_type`       | `AdsGetFieldType`     | ~2382 |
| `sqlite_field_length`     | `AdsGetFieldLength`   | ~2413 |
| `sqlite_field_decimals`   | `AdsGetFieldDecimals` | ~2465 |
| `sqlite_record_num`       | `AdsGetRecordNum`     | ~2632 |
| `sqlite_record_count`     | `AdsGetRecordCount`   | ~2667 |
| `sqlite_get_field`        | `AdsGetField`         | 2755-2773 |
| `sqlite_is_record_deleted`| `AdsIsRecordDeleted`  | ~3546 |
| `sqlite_open_index`       | `AdsOpenIndex`        | ~4265 |
| `sqlite_is_found`         | `AdsIsFound`          | ~6705 |

- [ ] **Step 3: Add the ops instance + `register_builtin_backends`**

Still inside the `#if defined(OPENADS_WITH_SQLITE)` block:
```cpp
const openads::abi::BackendTableOps* sqlite_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &sqlite_close_table;
        o.goto_top          = &sqlite_goto_top;
        o.goto_bottom       = &sqlite_goto_bottom;
        o.skip              = &sqlite_skip;
        o.at_eof            = &sqlite_at_eof;
        o.at_bof            = &sqlite_at_bof;
        o.num_fields        = &sqlite_num_fields;
        o.field_name        = &sqlite_field_name;
        o.field_type        = &sqlite_field_type;
        o.field_length      = &sqlite_field_length;
        o.field_decimals    = &sqlite_field_decimals;
        o.record_num        = &sqlite_record_num;
        o.record_count      = &sqlite_record_count;
        o.get_field         = &sqlite_get_field;
        o.is_record_deleted = &sqlite_is_record_deleted;
        o.open_index        = &sqlite_open_index;
        o.is_found          = &sqlite_is_found;
        return o;
    }();
    return &ops;
}
```
Then, OUTSIDE the `#if` (so it always exists), in the anonymous namespace:
```cpp
} // namespace  (close anon ns if needed, or keep within)
namespace openads::abi {
void register_builtin_backends() {
#if defined(OPENADS_WITH_SQLITE)
    register_backend_table_ops(openads::session::HandleKind::SqliteTable,
                               sqlite_table_ops());
#endif
}
}
```
(Place `register_builtin_backends` where `sqlite_table_ops` is visible; if the
ops live in the anonymous namespace, define `register_builtin_backends` in the
same TU after them.)

- [ ] **Step 4: Add includes** at the top of `ace_exports.cpp` (near other abi
  includes): `#include "abi/backend_table_ops.h"` and
  `#include "abi/backend_registry.h"`.

- [ ] **Step 5: Build**

Run: `cmake --build build\reg-msvc`
Expected: 0 errors. (Ops are defined but not yet called by the 17 functions.)

- [ ] **Step 6: Full suite green (behavior unchanged)**

Run: `build\reg-msvc\tests\openads_unit_tests.exe`
Expected: `0 failed`, same total as Task 1.

- [ ] **Step 7: Commit**

```
git add src/abi/ace_exports.cpp src/abi/backend_registry.h
git commit -m "feat(abi): lifted SQLite table ops + registry dispatcher (not yet wired)"
```

---

### Task 4: Wire the 17 ABI functions to the registry

Replace each inline `#if OPENADS_WITH_SQLITE { if (get_sqlite_table) {...} }`
block with a single registry lookup. Do it in three commits (nav, fields, misc)
so a reviewer can reject one batch independently; the suite must be green after
each.

**Files:** Modify `src/abi/ace_exports.cpp` (the 17 functions).

**Replacement pattern** (example: `AdsGotoTop`): delete lines 2101-2110 (the
`#if … #endif` SQLite block) and insert, right after the `get_remote_table`
block:
```cpp
    if (auto* ops = backend_table_ops_for(hTable))
        if (ops->goto_top) return ops->goto_top(hTable);
```
The native `Table* t = get_table(hTable); …` tail is left exactly as-is.

- [ ] **Step 1: Batch A — nav + close (4 fns)**

Wire `AdsCloseTable`(→`close_table`), `AdsGotoTop`(→`goto_top`),
`AdsGotoBottom`(→`goto_bottom`), `AdsSkip`(→`skip(hTable, lRows)`). Each: remove
the inline `#if` block, insert the two-line lookup using the matching op and its
exact args.

- [ ] **Step 2: Build + suite green**

Run: `cmake --build build\reg-msvc && build\reg-msvc\tests\openads_unit_tests.exe`
Expected: `0 failed`, same total.

- [ ] **Step 3: Commit**

```
git add src/abi/ace_exports.cpp
git commit -m "refactor(abi): route SQLite nav ops through the registry"
```

- [ ] **Step 4: Batch B — field accessors (8 fns)**

Wire `AdsAtEOF`(→`at_eof(hTable,pbAtEnd)`), `AdsAtBOF`(→`at_bof`),
`AdsGetNumFields`(→`num_fields`), `AdsGetFieldName`(→`field_name` with its 4
args), `AdsGetFieldType`(→`field_type`), `AdsGetFieldLength`(→`field_length`),
`AdsGetFieldDecimals`(→`field_decimals`), `AdsGetField`(→`get_field` with its 5
args). Use each op's exact parameter list from the vtable.

- [ ] **Step 5: Build + suite green** (same command). Expected: `0 failed`.

- [ ] **Step 6: Commit**

```
git add src/abi/ace_exports.cpp
git commit -m "refactor(abi): route SQLite field accessors through the registry"
```

- [ ] **Step 7: Batch C — record/index/found (3 fns + remaining)**

Wire `AdsGetRecordNum`(→`record_num`), `AdsGetRecordCount`(→`record_count` with
its filter arg), `AdsIsRecordDeleted`(→`is_record_deleted`),
`AdsOpenIndex`(→`open_index` with its 4 args), `AdsIsFound`(→`is_found`).

- [ ] **Step 8: Build + suite green** (same command). Expected: `0 failed`.

- [ ] **Step 9: Verify zero remaining inline SQLite table dispatch**

Run: `git grep -n "get_sqlite_table(hTable)" src/abi/ace_exports.cpp`
Expected: only the matches inside the lifted `sqlite_<op>()` functions (Task 3),
none inside `Ads*` ABI functions.

- [ ] **Step 10: Commit**

```
git add src/abi/ace_exports.cpp
git commit -m "refactor(abi): route SQLite record/index ops through the registry"
```

---

### Task 5: Final verification + reviewer gate

**Files:** none (verification).

- [ ] **Step 1: Clean rebuild**

Run: `cmake --build build\reg-msvc --clean-first`
Expected: 0 errors, 0 warnings introduced by the change.

- [ ] **Step 2: Full suite**

Run: `build\reg-msvc\tests\openads_unit_tests.exe`
Expected: `Status: SUCCESS!`, `0 failed`, total = Task 0 baseline + 2 new unit
cases (Tasks 1 and 2).

- [ ] **Step 3: Diff sanity — native path untouched**

Run: `git diff 973d6f3 -- src/abi/ace_exports.cpp | findstr /C:"get_table(" /C:"get_remote_table("`
Expected: no native/remote dispatch lines were modified (only SQLite `#if`
blocks were removed and the two-line lookups added).

- [ ] **Step 4: Request supervised review** (regra 7) before merging to the
  integration line. Summarize: files changed, suite number before/after,
  confirmation native/remote dispatch unchanged.

---

## Self-Review

**Spec coverage:** mechanism (`backend_table_ops.h` + `backend_registry.*`) →
Task 2; `HandleKind` lookup → Task 1 + Task 3 dispatcher; lifted SQLite ops +
registration + once-init → Task 3; 17 ABI functions wired → Task 4; native
fall-through untouched → constraint enforced + verified Task 5 Step 3;
behavior-preserving via unchanged suite → every task's green gate. Covered.

**Placeholder scan:** the only intentionally-templated steps are the 14
identical-shape lifts in Task 3 (Step 2), each pinned to an exact source ABI
function + line range with two fully-worked shape exemplars (nav, get_field) and
one metadata exemplar — an implementer copies the named inline block verbatim. No
TBD/TODO.

**Type consistency:** `BackendTableOps` member names and signatures in Task 2 are
reused verbatim in the `sqlite_table_ops()` wiring (Task 3) and the Task 4
call-sites; `kind_of`/`ops_for_kind`/`backend_table_ops_for`/
`register_builtin_backends`/`register_backend_table_ops` names are consistent
across Tasks 1-4.
