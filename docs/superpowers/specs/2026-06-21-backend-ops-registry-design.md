# Design — Pluggable backend-ops registry for `ace_exports.cpp`

**Date:** 2026-06-21
**Base:** `973d6f3` (`pr/openads-plus-nav-setdouble-remote` = upstream + sqlite +
sql-passthrough + AdsSetDouble/AdsSetLogical remote fixes)
**Work branch:** `refactor/backend-ops-registry`

## Problem

Each SQL backend (sqlite, sqlcipher, postgres, mariadb, odbc, and the future
Firebird/MSSQL/Oracle + Grok's ODBC-universal line) adds its table-level dispatch
as an inline `#if defined(OPENADS_WITH_X) { if (auto* st = get_X_table(h)) {...} }`
block inside **every** ABI function that needs it. On the current base, SQLite
alone has **17** such call-sites:

`AdsCloseTable`, `AdsGotoTop`, `AdsGotoBottom`, `AdsSkip`, `AdsAtEOF`,
`AdsAtBOF`, `AdsGetNumFields`, `AdsGetFieldName`, `AdsGetFieldType`,
`AdsGetFieldLength`, `AdsGetFieldDecimals`, `AdsGetRecordNum`,
`AdsGetRecordCount`, `AdsGetField`, `AdsIsRecordDeleted`, `AdsOpenIndex`,
`AdsIsFound`.

Adding N backends multiplies that: 17 functions × N backends ≈ the ~100 merge
hunks observed in `ace_exports.cpp` when combining the per-backend PR branches.
A naive `union` git-merge corrupts the file (duplicated blocks → C2371,
unbalanced `#if/#endif` → C1070). The per-function `#if` ladder is the root
cause.

## Goal

Move the per-backend `#if OPENADS_WITH_X` **out** of the ~17 ABI functions and
into a single registration point, so:

- Each ABI function dispatches through **one** backend-agnostic lookup.
- A new backend = fill one struct + add one registration line. Near-zero
  conflict surface in `ace_exports.cpp`.
- True pluggability for Firebird/MSSQL/Oracle and Grok's ODBC-universal line.

## Non-goals / hard constraints

- **The native path is first-class and untouched.** Local DBF/ADT and the native
  client/server protocol (`tcp://`, `RemoteTable`) do **not** register ops; they
  remain the unchanged fall-through in every function. Native works in both
  client/server and enterprise modes; nothing is forced — the caller chooses the
  connection mode by URI. (Owner's north-star: coexistence, no forcing.)
- This is a **behavior-preserving refactor**, not a feature change. The existing
  unit suite must stay green with no test edits.
- No change to the public ACE ABI surface.

## Architecture

Three new, isolated units:

### 1. `src/abi/backend_table_ops.h`
Defines the well-defined boundary a backend implements:

```cpp
struct BackendTableOps {
    UNSIGNED32 (*close_table)     (ADSHANDLE);
    UNSIGNED32 (*goto_top)        (ADSHANDLE);
    UNSIGNED32 (*goto_bottom)     (ADSHANDLE);
    UNSIGNED32 (*skip)            (ADSHANDLE, SIGNED32);
    UNSIGNED32 (*at_eof)          (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*at_bof)          (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*num_fields)      (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*field_name)      (ADSHANDLE, UNSIGNED16, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*field_type)      (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*field_length)    (ADSHANDLE, UNSIGNED8*, UNSIGNED32*);
    UNSIGNED32 (*field_decimals)  (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*record_num)      (ADSHANDLE, UNSIGNED32*);
    UNSIGNED32 (*record_count)    (ADSHANDLE, UNSIGNED32*, UNSIGNED16);
    UNSIGNED32 (*get_field)       (ADSHANDLE, UNSIGNED8*, UNSIGNED8*, UNSIGNED32*, UNSIGNED16);
    UNSIGNED32 (*is_record_deleted)(ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*open_index)      (ADSHANDLE, UNSIGNED8*, ADSHANDLE*, UNSIGNED16*);
    UNSIGNED32 (*is_found)        (ADSHANDLE, UNSIGNED16*);
};
```
Signatures mirror the corresponding ABI functions exactly. Each function pointer
may be left `nullptr` if a backend doesn't support that op (the dispatcher then
falls through / returns the same error the inline code does today). The exact
final argument lists are copied verbatim from the current ABI signatures during
implementation.

### 2. `src/abi/backend_registry.{h,cpp}`
```cpp
void register_backend_table_ops(HandleKind table_kind, const BackendTableOps* ops);
const BackendTableOps* backend_table_ops_for(ADSHANDLE h);  // nullptr if not a registered backend
void register_builtin_backends();   // the ONLY place with #if OPENADS_WITH_X
```
- The registry is a small map `HandleKind -> const BackendTableOps*` (≤ a handful
  of entries; a fixed array indexed by kind or an `unordered_map`).
- `backend_table_ops_for(h)` reads the handle's `HandleKind` from the session
  registry and returns the registered ops, or `nullptr` for native/remote kinds
  (`Table`, `RemoteTable`) → fall-through.
- `register_builtin_backends()` contains, e.g.:
  ```cpp
  #if defined(OPENADS_WITH_SQLITE)
      register_backend_table_ops(HandleKind::SqliteTable, sqlite_table_ops());
  #endif
  // (postgres, mariadb, odbc … added here later — one line each)
  ```

### 3. Per-backend ops accessor
Each backend file exposes `const BackendTableOps* sqlite_table_ops();` returning a
pointer to a file-local `static const BackendTableOps` wired to that backend's
existing logic (`get_sqlite_table` + the bodies currently inline in
`ace_exports.cpp`, lifted into thin functions). The `#if OPENADS_WITH_SQLITE`
lives in the backend file and the one registration line — never again in the ABI
functions.

## Dispatch transformation

Each of the 17 ABI functions changes from:
```cpp
#if defined(OPENADS_WITH_SQLITE)
    if (auto* st = get_sqlite_table(hTable)) { /* sqlite body */ return rc; }
#endif
    /* remote (tcp://) + native body — unchanged */
```
to:
```cpp
    if (auto* ops = backend_table_ops_for(hTable))
        if (ops->goto_top) return ops->goto_top(hTable);
    /* remote (tcp://) + native body — unchanged */
```
The sqlite body moves verbatim into `sqlite_<op>()` in the backend file.

## Initialization

No `DllMain` exists. `register_builtin_backends()` runs once via a function-local
static inside `backend_table_ops_for`:
```cpp
const BackendTableOps* backend_table_ops_for(ADSHANDLE h) {
    static const bool _ = (register_builtin_backends(), true); (void)_;
    ...
}
```
Function-local statics are thread-safe-once (C++11) and **always run** because
the enclosing function is called by the ABI layer — unlike a namespace-scope
static in the `openads_core` STATIC lib, which the MSVC linker can strip when
building the `openace64` SHARED DLL. This is why registration is **explicit**,
not self-registration.

## Coexistence (native + enterprise, by URI)

Native local DBF and native client/server (`tcp://`) handles use `HandleKind`
values that are **not** registered → `backend_table_ops_for` returns `nullptr` →
the original native/remote code runs untouched. SQL/ODBC backends are reached
only when the caller opened that kind of connection (chosen by URI). Native and
enterprise modes coexist; neither is forced.

## Scope (this session)

1. Add the three units (header, registry, sqlite ops accessor).
2. Lift the 17 SQLite dispatch bodies into `sqlite_<op>()` functions; replace the
   17 inline `#if` blocks with the single registry lookup.
3. Build the SQLite configuration and run the existing unit suite.

**pg / maria / odbc / sqlcipher and future Firebird/MSSQL/Oracle are out of scope
here** — they follow later by adding their ops accessor + one registration line.

## Testing

- The existing unit suite already exercises the SQLite dispatch paths
  (`AdsGetField`, `AdsGotoTop`, etc. over SqliteTable handles). After the
  refactor it must stay **green with zero test edits** — that is the
  behavior-preserving proof. Baseline before the refactor: 528/528 (44,666
  assertions) on the ODBC build of an equivalent tree.
- Build the SQLite config (`OPENADS_WITH_SQLITE=ON`), run
  `build\<dir>\tests\openads_unit_tests.exe`; require 0 failed.

## Risks & mitigations

- **Touching core dispatch.** Mitigated: only the SQL branch is *lifted*; the
  native/remote branch is copied unchanged. The suite is the guard-rail (green
  before and after). Supervised merge per regra 7.
- **Linker stripping the registration.** Mitigated by explicit registration via a
  function-local static in a called function (see Initialization).
- **Signature drift** when copying ABI arg lists into the ops struct. Mitigated by
  copying verbatim from the current declarations during implementation; the
  compiler enforces an exact match at the assignment of each function pointer.

## Future (out of scope, enabled by this)

Adding a backend becomes: implement `BackendTableOps` for it, expose
`X_table_ops()`, add one `#if`-guarded line to `register_builtin_backends()`. No
edits to the 17 ABI functions → the ~100-hunk merge problem is gone. Grok's
ODBC-universal line and Firebird/MSSQL/Oracle slot in the same way.
