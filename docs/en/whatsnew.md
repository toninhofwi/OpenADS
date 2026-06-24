---
title: What's New
layout: default
parent: Home (EN)
nav_order: 0
permalink: /en/whatsnew/
---

# What's New (v1.0.0-rc29 → v1.2.2)

This page summarises the most notable changes since the
v1.0.0-rc29 release. For the full commit-by-commit history see
the [CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## New Features

### SQLite-backed Table Driver

An optional SQLite-backed table driver is now available behind
the `OPENADS_WITH_SQLITE` CMake flag. When enabled, the engine
can open and manipulate tables through a SQLite backend, providing
an alternative storage layer. New source files:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### SQL Backends — PostgreSQL / MariaDB / ODBC (OpenADS Plus)

OpenADS can now open tables on **PostgreSQL**, **MariaDB / MySQL**
and any **ODBC**-reachable engine behind the ACE ABI, selected by
the connection URI (`postgresql://` / `mariadb://` / `odbc://`),
exactly like the SQLite backend. From the application's point of
view the table behaves like any other work area — navigation,
field read, and column SEEK all work.

These four SQL backends sit behind a single **pluggable
backend-ops registry**: each registers one `BackendTableOps`
struct (17 function pointers mirroring the table ops) so the ~17
ABI navigation / field functions stay backend-agnostic instead of
multiplying a per-backend `if` block across each. Adding another
backend (e.g. Firebird, or MSSQL / Oracle via ODBC) is one ops
struct plus one registration line. The native local DBF / ADT and
the `tcp://` remote paths are unchanged fall-throughs. Identifiers
are validated to safe ASCII and SEEK values use prepared-statement
parameters (no string concatenation). See `docs/OPENADS_PLUS.md`.

### ADT Validation Patches (F1–F7, R1–R3)

ADT table validation now includes a full set of structural
patches (F1–F7) and record-level checks (R1–R3), strengthening
the integrity guarantees for `.adt` files produced by SAP
Advantage.

### Native ADT/ADI Create, Read, Write, and Index Seek

OpenADS can now operate end-to-end on native `.adt` / `.adi` /
`.adm` files:

- **Create** — `AdsCreateTable(ADS_ADT)` writes a valid table
  header, field descriptors, and an optional `.adm` memo store.
- **Write** — `AdsAppendRecord` / `AdsWriteRecord` persist rows
  and memo payloads.
- **Read** — Re-open, field get, record count, memo round-trip.
- **Index** — `AdsCreateIndex61` builds `.adi` bags (first tag
  via `AdiIndex::create`, additional tags via `add_tag`).
- **Seek** — `AdsSeek` on character and numeric ADI keys.
- **AUTOINC** — counter seeded from existing rows at open time;
  descriptor tail bytes 139–143 stay zero on disk.
- **ADM memo layout** — 8-byte blocks with a 1024-byte metadata
  prefix.

### ADI Write Path

The ADI index driver now supports write operations — `insert`,
`erase`, and `flush` — including dense-leaf recno decoding and
character-key seek. This completes the read-write cycle for ADI
indexes.

### Trigger Dispatch (BEFORE / INSTEAD_OF / AFTER)

Triggers now fire with proper timing dispatch:

- **BEFORE** — runs before the DML statement.
- **INSTEAD_OF** — replaces the DML on views.
- **AFTER** — runs after successful execution.

Priority sort, `__error` table for failures,
`sp_DisableTriggers` / `sp_EnableTriggers` stored procedures,
and composite trigger keys in `system.triggers` are all
supported.

### DA-Web Management UI

The browser-based Data Architect replacement (**DA-Web**) has
received extensive work:

- **Inline cell editing** with dirty tracking and visual
  feedback.
- **Index management** — save and delete indexes via API.
- **Trigger CRUD** — add, delete, and edit triggers with
  splitter and inline validation.
- **AOF (Rushmore) filter toolbar** in the table browser.
- **ADS SQL syntax highlighting** with HeidiSQL-like colours.
- **Stored procedure / function source viewer** with parameter
  grid and Save-to-DD.
- **Index tag browser**, field type labels, and SQL scripts.
- **Connection menu** — New DD, Open DD, Free Tables.
- **Effective Permissions** and **Members** tabs on user/group
  panels.
- **RI tag dropdowns** populated from binary `.add` files.

### openmonitor — TUI and Web Dashboard

A new `openmonitor` tool provides both a terminal UI (TUI) and a
web dashboard for monitoring and administering `openads_serverd`.

### OpenADS Studio — Responsive (phone / tablet)

The Studio web console now adapts to small screens — usable from a
phone or tablet, not just a desktop browser. Below ~768 px the
table-list sidebar becomes a slide-in **drawer** (☰ in the header,
dimmed backdrop, auto-close on select); the tab bar scrolls
horizontally and forms / modals reflow to a single column with
larger touch targets. A long-standing dark-theme bug — the
`--panel` / `--panel-2` / `--border` CSS variables were
self-referential, so panels and borders rendered transparent — is
fixed as part of this work.

### Remote Scan Performance (sequential prefetch)

A forward scan over the `tcp://` wire used to cost roughly one TCP
round-trip per record (`Skip` + `AtEOF` + `IsFound`). A
sequential-prefetch path (negotiated via a Connect capability flag)
now piggybacks a lookahead block of rows onto forward-`Skip` acks;
the client serves them locally and folds the consumed count back
into the next wire step so the server cursor never desyncs.
`AdsAtEOF` / `AdsAtBOF` are answered from the cached current row and
`AdsIsFound` from a cached `Found()` flag. Result: a 50k-record
loopback scan is **~3.9× faster** (NAV-only) / **~3.3×** (3-field
read), with `IsFound` round-trips dropping to zero. The change is
additive and backward-compatible — old clients (no capability
advertised) keep the previous wire behaviour byte-for-byte.

### PHP Native Extension (`php_openads`)

A native Zend PHP extension (`php_openads.dll`) is now available
for PHP 8.x, providing full DD CRUD (35 new `AdsDictionary`
methods), date/timestamp field decoding, and per-statement field
name caching.

### SAP Data Dictionary Import Improvements

- `import_dd` now copies `.am` memo files and decodes encrypted
  function bodies.
- Group membership import (DB:Admin, DB:Backup, DB:Debug) from
  binary `.add` files.
- Trigger timing captured from `system.triggers`.
- `grant_permission` and `AE_SAP_PERMS_NEED_IMPORT` error code
  for permission migration.

### WAL Crash Recovery

WAL (Write-Ahead Log) crash recovery now handles `APPEND`
records, completing the ARIES-lite recovery model.

### DD SQL Expansion

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- `sp_*` stored procedures.
- `system.*` virtual tables (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, stored procedures,
  views, and index properties.
- Per-table access control with user/group permission levels.

---

## Bug Fixes

### Engine

- **CDX empty-leaf walk** — forward and backward index walks now
  skip empty leaves left behind by `erase()`. Fixes REINDEX /
  bulk-delete `ADSCDX/5000`. (PR #63)
- **CDX leaf recno bits** — `compute_layout` sizes the
  record-number field from `max_rec`, not just key length, so
  wide-key tags no longer truncate recnos ≥ 4096. (PR #62)
- **CDX prefix seek** — `seek_key` compares only the search-key
  length, so partial seeks like `SEEK "ART-00024800"` match stored
  `"ART-00024800 desc ..."` keys. (PR #62)
- **MSSQL backward SKIP** — off-by-one: `abs_n == pos` now reaches
  row 0 instead of reporting BOF. (PR #65)
- **ABI typed getters for SQL backends** — `AdsGetDouble`/`Long`/`
  LongLong`/`String` dispatch through the backend ops vtable, so
  PostgreSQL returns real values. (PR #66)
- **`AdsGetIndexHandle` for SQL backends** — resolves by-name for
  PG tables so indexed seek works end-to-end. (PR #66)
- **NTX numeric key format** — numeric fields indexed into an NTX
  bag now store keys in the native DBFNTX form (zero-padded
  magnitude + complemented negatives) instead of space-padded
  `STR()` text. A native xBase reader's `dbSeek(<number>)` now
  matches the on-disk key. Reopened index bags retain the numeric
  encoding. (PR #67)
- **Numeric `dbSeek`** — rddads sends `ADS_STRING` key type for
  numeric seeks; the engine now handles this correctly.
- **`ALIAS->FIELD` in numeric seek** — strip `ALIAS->` prefix so
  aliased CDX tags find keys.
- **Transaction rollback** — physically removes appended records
  on rollback instead of leaving ghost rows.
- **LockMgr held-state leak** — `held_` is now cleared on unlock
  so the next lock takes a real OS lock.
- **`AdsGetRecordCount`** — now respects `bFilterOption`.
- **`AdsSetRelation`** — fails honestly when appropriate.
- **`seek_key`** — `walk_to_last` now honours `SET DELETED ON`.
- **Empty table navigation** — correctly handles tables with zero
  records.
- **Deleted-record navigation** — proper state after skip past
  deleted rows.
- **LockMgr lock refcount** — repeated locks on the same key are
  now refcounted; the OS lock is released only when the final
  holder unlocks.
- **WAL record bounds checks** — `TxLog::read_all` validates each
  UPDATE/APPEND field length before reading, so truncated or
  corrupt WAL files no longer over-read.

### ABI

- **Out-of-bounds read** in numeric key formatting — prevented.
- **Swapped option bits** — `ADS_DESCENDING` (0x02) and
  `ADS_COMPOUND` (0x08) were decoded incorrectly in
  `AdsCreateIndex61`.
- **Case-insensitive field-name resolution** — `field_index` is
  now cached for performance.
- **`AE_NO_CURRENT_RECORD`** — returns 5068 instead of 5026.
- **`OrdListAdd`** — falls back to basename when a relative path
  double-prefixes the table directory.
- **Trig helper linkage** — C++ linkage for `trig_*` helpers to
  silence MSVC C4190.

### DA-Web Security

- AOF filter expression sanitisation.
- Drive-root path containment fix.
- API security sweep across all PHP endpoints.
- RI meta index paths contained under DD directory.
- Wire frame size caps to prevent abuse.

### Platform / Build

- **macOS** — sign-conversion and unused-function warnings fixed.
- **GCC** — `-Werror` warnings resolved (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — x86 `__stdcall` decoration mismatch and x64
  `_wfsopen` crash fixed.
- **Clang** — `-Wc2y-extensions` guard for older Apple Clang.
- **POSIX fd-0 collision** — file handles are now stored as
  `(fd + 1)` so a real fd 0 (returned by `open()` when stdin is
  closed) is no longer mistaken for the "not open" sentinel.
- **POSIX `EINTR` retry** — `pread` / `pwrite` retry on signal
  interruption instead of failing the I/O.
- **POSIX zero-length mmap** — `map_readonly` rejects zero-length
  maps instead of calling `mmap` with length 0.

### CDX

- Shared header lock on open so concurrent appends don't fail.
- Structural CDX named per-table, not per-directory session.
- **Stale record-count refresh on the fetch path** — a driver
  caches the DBF record count at open; in a multiuser deployment a
  peer append could leave the cache lagging, so an index walk that
  reached a just-appended row (e.g. mid-`REPLACE … FOR`) failed
  hard with spurious error 5000. `read_record_raw` /
  `write_record_raw` now re-read the on-disk count under a shared
  header lock before declaring a recno out of range (slow path
  only — a normal forward scan pays nothing).

### Remote (Wire)

- Use-after-free crash on virtual table queries via TCP
  eliminated.

---

## Documentation

- **CONTRIBUTING.md** — new contribution guide with PR workflow,
  protocol policy, and clean-room rules.
- **Wire Protocol DD API** — comprehensive Data Dictionary API
  reference (§9) added.
- **DA-Web GUIDE.md** — full user guide for the DA-Web browser
  interface.
- **README** — comprehensive post-rc29 status update covering
  ADI write path, ADT creation, AES mode, and DA-Web scope.
- **Cookbook** — new `cookbook/` folder with runnable,
  heavily-commented Harbour examples (simple → advanced). The
  `console/` track is pure xBase: create / index seek /
  transactions / DBF maintenance (`ADSCDX`), SQL via
  `AdsCreateSQLStatement` + `AdsExecuteSQLDirect`, native ADT
  (`ADSADT` + `.adi`), and a `tcp://` remote client. The `orm/`
  track runs the same CRUD through a companion Harbour ORM across
  SQLite / DBF / PostgreSQL / MariaDB / ODBC back-ends, capped by an
  all-back-ends benchmark (`orm/complete/`) with a cross-back-end
  content checksum and a seek-vs-scan headline. A FiveWin
  `xbrowse` CRUD sample and connection-string / field-type /
  troubleshooting guides round it out.

---

## Testing

- **738 unit tests** passing across all platforms (349 231
  assertions).
- New test files: `abi_ntx_numeric_edge_test.cpp` (NTX numeric
  edge cases: -0.0, clamping, complement, custom key),
  `cdx_empty_tree_test.cpp` (empty tree, all-erased tree, exact
  prefix, descending prefix), `abi_ntx_numeric_key_test.cpp`,
  `cdx_reindex_char_test.cpp`, `cdx_prev_empty_leaf_test.cpp`,
  `cdx_multitag_2nd_test.cpp`, `cdx_prefix_seek_test.cpp`,
  `mssql_table_skip_test.cpp`.
- Harbour demo in `examples/adt-native/` (by glokcode).
