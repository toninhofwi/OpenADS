# harbour-orm

A small, open **ActiveRecord-style ORM for Harbour**, multi-backend, built on the
**OpenADS ACE ABI** (`openace64.dll` / `openace32.dll`). The same model code runs
against several backends — you pick the backend **per deployment, by connection
URI**, never hardwired into your queries.

## Why

Harbour apps that speak the ACE ABI (`USE` / `SKIP` / `SEEK`, or SQL) can target
very different deployments — a single-user embedded database, a native SQL
server, or a client/server engine — without rewriting data-access code. This ORM
gives that code a modern shape (models, a fluent query builder, relations,
migrations) while keeping the **coexistence of connection modes**: the deployer
chooses, by the connection string.

## Connection modes (selected by URI)

| URI | Mode |
|-----|------|
| `sqlite://<file>` | embedded SQLite, SQL path |
| a local directory | native DBF/ADT tables, in-process, navigational path |
| `tcp://host:port/<dir>` · `tls://…` | client/server over the wire |
| `postgresql://…` · `mariadb://…` · `odbc://…` | navigational backends |

Same model code across all of them — only the URI changes.

## Features

- **`TORMModel`** — ActiveRecord base: `Create` / `Find` / `Save` / `Delete`,
  attribute get/set, persisted-state tracking.
- **`TORMQuery`** — fluent builder: `Where` / `OrderBy` / `Limit` / `Get`.
- **`TORMGrammar`** — dialect-agnostic AST → SQL renderer (one grammar per
  dialect).
- **Relations** — `HasMany` / `HasOne` / `BelongsTo` via codeblock factories.
- **`TORMSchema` / `TORMBlueprint`** — schema builder / migrations (DDL).
- **Two execution paths** — a SQL path for SQL-capable backends and a
  **navigational path** (table cursor) for xBase/native and navigational-only
  backends, so a `Find` honors deletion correctly and writes work where there is
  no SQL passthrough.

## Layout

```
src/        ORM classes + the ACE glue (hbo_ace.prg, BEGINDUMP)
include/    hborm.ch
tests/      smoke.prg (SQL path) + exhaust.prg (per-backend CRUD harness)
```

## Building (64-bit, Harbour + OpenADS)

Set these and run `build_run.bat` (smoke) or `build_exhaust.bat` (harness):

- `HB_BIN` — 64-bit `hbmk2.exe`
- `OPENADS_DLLDIR` — directory with `openace64.dll` + `openace64.lib`
- `OPENADS_INCDIR` — directory holding `openads/ace.h` (for the C glue)
- `MSVC_SETUP` — MSVC x64 env `.bat` (optional if already in the environment)

The harness runs SQLite and a local DBF directory out of the box; set
`OADS_SERVER_URI` / `OADS_PG_URI` / `OADS_MARIA_URI` / `OADS_ODBC_URI` to include
those backends.

## License

MIT — see [LICENSE](LICENSE).
