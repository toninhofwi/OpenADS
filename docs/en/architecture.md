---
title: Architecture
layout: default
parent: Home (EN)
nav_order: 2
permalink: /en/architecture/
---

# Architecture

OpenADS is a five-layer system. Each layer is a swap-out point
that an application or test can hit independently.

```
L1  ABI               extern "C" Ads* exports
                      (ace32/64.dll, libace.so/.dylib)
L2  Session           Connection / Statement / HandleRegistry / Tx
L3  SQL engine        Lexer → Parser → Resolver → Planner → Executor
                      AEP host, xBase UDFs
L4  Engine core       Table / Index / MemoStore / Cursor / LockMgr
                      TxLog (WAL) / Catalog
L5  Platform          File / mmap / byte-range locks / sockets / DLL
                      Win32 + POSIX implementations
```

## Layer responsibilities

| Layer | What it owns |
|-------|--------------|
| **L1** | The only module with a C ABI. Translates `Ads*` calls into the internal C++ API; converts ACE error codes to/from `util::Error`; performs OEM / ANSI / UTF-8 / UTF-16 conversions. |
| **L2** | Per-connection state — opened tables, prepared SQL statements, transaction stack, AEP procedure registry, encryption key. |
| **L3** | Full Advantage SQL dialect — boolean WHERE trees, joins (INNER / LEFT / RIGHT / FULL OUTER), subqueries (correlated + uncorrelated), GROUP BY + HAVING, UNION, window functions, CTEs, CASE, scalar / aggregate / arithmetic projection. |
| **L4** | Format-agnostic engine — `Table`, `Index`, `MemoStore`, `Cursor`, `LockMgr`, `TxLog`, `Catalog`. The `Driver` trait is the extension point for new file formats. |
| **L5** | Cross-platform OS abstraction (Win32 + POSIX). |

## Drivers (L4 extension point)

```
AdtDriver    .adt + .adm + .adi    (proprietary ADS — out of scope)
CdxDriver    .dbf + .cdx + .fpt    (FoxPro)
NtxDriver    .dbf + .ntx + .dbt    (Clipper)
VfpDriver    .dbf + .cdx + .fpt    (Visual FoxPro variants)
```

Each driver implements the same trait, so `Table::open(path)`
returns a polymorphic handle the rest of L4 / L3 doesn't have
to specialise on.

## Server daemon

`openads_serverd` runs L2–L5 in-process and exposes them over
the OpenADS-native wire protocol on a TCP port (cleartext
`tcp://` or TLS `tls://`, since v0.4.0). The same DLL that
talks to a local data dir also speaks to a remote
`openads_serverd` via a `tcp://host:port/<dir>` URI — see
[Wire protocol](../wire-protocol/) for the on-the-wire bytes.

## Studio (web console)

`OPENADS_WITH_HTTP=ON` is the build default since v1.0.0-rc20.
The same Studio SPA is served by two hosts:

- **Remote Server mode** — embedded in `openads_serverd.exe`,
  serving the wire protocol and the HTTP console side-by-side.
- **LocalServer mode** (since v1.0.0-rc9) — embedded in
  `ace64.dll` / `ace32.dll` itself. A Harbour / X# / Clipper
  app that loads the OpenADS DLL gets the Studio web console
  in its own process. Three OpenADS-only exports drive it —
  `AdsStudioStart` / `Stop` / `Port` — plus an
  `OPENADS_STUDIO_PORT` env-var auto-start hook from
  `DllMain`. Studio's header carries a mode badge (since rc10)
  that distinguishes the two modes via `/api/health`'s `mode`
  field.

Each REST request opens a short-lived ABI connection, so the
console is **just another consumer of the public ABI** — same
as a Harbour app.
