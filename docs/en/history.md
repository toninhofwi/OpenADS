---
title: Project History
layout: default
parent: Home (EN)
nav_order: 1
permalink: /en/history/
---

# Project History

OpenADS grew from a minimal skeleton into a full ADS-compatible
engine over **868 commits** organised as milestone-driven batches.
This page traces the major phases.

---

## M0–M2: Foundations (2025)

The first commits established the build system, ABI thunks, and
the core table lifecycle.

| Milestone | What landed |
|-----------|-------------|
| **M0** | CMake scaffold, `HandleRegistry`, `Connection`, doctest framework |
| **M1** | DBF header parser shared by CDX/NTX/VFP, `Cursor` state machine (Bof/Positioned/Eof/Limbo), read-only `CdxDriver` |
| **M2** | Append / update / delete records, `LockMgr` byte-range locks (Compatible mode), NTX driver as label-only stub |

---

## M3: Indexes — the xBase heart (2025)

B+tree implementations for both Clipper and FoxPro index formats.

- **NTX** — full read + write + create + multi-level leaf/branch split
- **CDX** — multi-tag compound index, leaf splits, sibling links
- `AdsSeek`, `AdsSetOrder`, `AdsSetScope` / `AdsClearScope`
- First 100% pass of Harbour's `rddtst.prg` (442 / 442)

---

## M4–M6: Native formats & transactions (2025)

| Milestone | What landed |
|-----------|-------------|
| **M4** | ADT / ADI native create/read/write/seek, ADM memo, VFP field types (V/Q), AES-256-CTR encrypted tables |
| **M5** | WAL with ARIES-lite crash recovery, nested BEGIN/COMMIT, savepoints |
| **M6** | ADI index keys for Date / Time / Timestamp / Money / Logical |

---

## M10: SQL engine (2025–2026)

A massive ~53-milestone run that built a production SQL layer:

- SELECT, JOINs (INNER / LEFT / RIGHT / FULL), subqueries, CTEs
- GROUP BY + HAVING, UNION / UNION ALL, DISTINCT, LIMIT / OFFSET
- CASE WHEN, ROW_NUMBER(), RANK / DENSE_RANK, PARTITION BY
- INSERT INTO … SELECT, CREATE TABLE AS SELECT
- Full-text search (CONTAINS, LIKE, BETWEEN)
- Scalar functions, column aliases, `system.*` virtual tables

---

## M11: Advanced features (2026)

Collation, OEM / UTF-8 conversion, NULL bitmap, AEP host
(CREATE / EXECUTE PROCEDURE), and OpenADS-encrypted DBF
(AES-256-CTR).

---

## M12: The network — from local to remote (2026)

The most ambitious phase: turning OpenADS into a networked
database server.

| Milestone | What landed |
|-----------|-------------|
| M12.1–M12.3 | Socket layer, server accept loop, Hello / Connect dispatch |
| M12.4–M12.5 | Dual-mode DLL — `tcp://` URI routes ABI to the server |
| M12.6–M12.9 | Remote write surface, SQL exec, reindex, server auth |
| M12.10–M12.13 | Error propagation, batch row fetch, **real TLS** (mbedtls 3.6) |
| M12.14–M12.19 | Remote field metadata, index handles, row cache, record-count cache |
| M12.22–M12.23 | Full X# Advantage RDD compatibility (local + remote) |

---

## Studio: embedded web console (2026)

14 iterations (`studio.web.0.1` → `0.14`) produced a complete SPA:

- Paginated browse with inline CRUD, SQL editor, Sessions tab
- Data Dictionary viewer, CREATE INDEX wizard, Reindex / Pack / Zap
- HTTP Basic auth, theme toggle, ZIP backup, memo hex viewer
- JSON export, AOF (Rushmore) filter toolbar

---

## OpenADS Plus: multiple SQL backends (2026)

A pluggable `BackendTableOps` registry let new backends be added
without duplicating ABI logic:

| Backend | Driver |
|---------|--------|
| SQLite | built-in (`OPENADS_WITH_SQLITE`) |
| SQLCipher | SQLite3 Multiple Ciphers |
| PostgreSQL | libpq native |
| MariaDB / MySQL | native |
| ODBC | any driver (Firebird, SQL Server, …) |
| MSSQL | TDS 7.4 native (PR #53) |

---

## DA-Web: browser-based Data Architect (2026)

A full PHP frontend for administering OpenADS:

- SQL editor with syntax highlighting, inline table editing
- Data Dictionary CRUD (users, permissions, triggers, SPs, views)
- Import wizard from SAP binary `.add` files
- Referential Integrity visual editor, effective permissions viewer

---

## Ecosystem (2026)

| Component | Description |
|-----------|-------------|
| **PHP Bindings** | `openads/openads-php` — pure FFI package with OOP API |
| **Harbour ORM** | ActiveRecord ORM for Harbour over the ACE ABI |
| **openmonitor** | TUI + web dashboard for `openads_serverd` |
| **Cookbook** | Runnable Harbour examples: console, ORM, FiveWin |
| **Docs** | Trilingual site (EN / ES / PT) via GitHub Pages |

---

## Release timeline

| Version | Date | Highlights |
|---------|------|------------|
| 0.3.0 | 2025 | M10 SQL engine, M11 features |
| 0.3.6 | 2026-01 | M12 wire skeleton, TLS, serverd |
| v1.0.0-rc1 | 2026-03 | AOF (Rushmore), Studio web |
| v1.0.0-rc9 | 2026-04 | Embedded Studio (LocalServer) |
| v1.0.0-rc14 | 2026-04 | Windows Service + systemd |
| v1.0.0-rc19 | 2026-05 | X# RDD compat |
| v1.0.0-rc25 | 2026-05 | Index correctness sweep |
| v1.0.0-rc27 | 2026-06 | AdsGetField CHAR padding |
| v1.0.1 | 2026-06 | Cookbook, responsive Studio |
| v1.0.3 | 2026-06 | Remote scan perf, ODBC hardening |
| v1.0.4 | 2026-06 | CDX stale record-count fix |
| v1.1.0 | 2026-06 | PostgreSQL / MariaDB / ODBC backends |
| v1.2.0 | 2026-06 | Deferred-flush bulk insert (528×), MSSQL TDS 7.4 |
| v1.2.1 | 2026-06 | Unit tests, remote benchmarks, NTX numeric key fix |

---

## Development pattern

The project follows a **milestone-driven** approach: each `M`
represents a self-contained feature with tests. Numbered PRs
(#4–#67) carry focused fixes and features. Quality is enforced
with `-Werror` across MSVC, GCC, and Clang, and the test suite
grew from 0 to **720 / 720** passing assertions.
