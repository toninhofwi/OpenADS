---
title: What's New
layout: default
parent: Home (EN)
nav_order: 0
permalink: /en/whatsnew/
---

# What's New (v1.0.0-rc29 → rc31)

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
  heavily-commented Harbour examples (simple → advanced): a
  `console/` track (pure `ADSCDX` xBase — create, index seek,
  transactions, DBF maintenance) and an `orm/` track (CRUD through
  a companion Harbour ORM across SQLite / DBF / PostgreSQL /
  MariaDB / ODBC back-ends), plus connection-string, field-type
  and troubleshooting guides.

---

## Testing

- **564 unit tests** passing across all platforms (48 127
  assertions).
- New test files: `abi_adi_create_test.cpp` (ADI create,
  multi-tag, populated skip/seek) and
  `abi_adt_scope_validation_test.cpp` (create-from-zero, dual-tag
  seek, memo round-trip, stress append, remote wire, serverd
  subprocess).
- Wilson NTX index smoke test added.
- Harbour demo in `examples/adt-native/` (by glokcode).
