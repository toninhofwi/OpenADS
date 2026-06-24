# ORM example over the OpenADS ACE ABI

Self-contained ActiveRecord + query-builder ORM (hb_orm2) running
entirely on top of OpenADS' `ace64.dll`.  The demo connects via
`sqlite://` so every call lands on the engine; the same code works
with `dbf://` (local DBF directory) and `tcp://` (OpenADS server).

> **Status: v1.1.0-alpha — scaling line.**  The API is stable and
> gate-green on the SQLite and DBF backends; multi-backend breadth and
> concurrency are on the roadmap below.  The full ORM source is vendored
> here under `src/`.  Released separately under MIT.

## What the demo shows

- Schema / migrations: `CreateTable` with typed columns
- Model CRUD: `Create`, `Find`, `Set`, `Save`
- Fluent query builder: `Where`, `OrderBy`, `Get`, aggregates (`Sum`, `Count`)
- Relations + eager loading with `with()` — anti-N+1 proven by query counter
- Bilingual PT/EN API (`Find`/`Buscar`, `Save`/`Salvar`, `All`/`Todos`, …)

## ORM features (vendored `src/`)

- Parametrized, bind-safe core; type casts / hydration; DB-first introspection & scaffolding
- Full query builder: `where`/`join`/`groupBy`/`having`/aggregates/pagination/raw SQL
- Relations: `hasMany`/`hasOne`/`belongsTo`/`belongsToMany` (N:N) + eager `with("a.b")` (nested) + `withCount`
- Index seek + batch insert/upsert · migrations (up/down) · transactions
- Navigational DBF backend (cursor path) + FiveWin grid binding
- **Soft-deletes** (`deleted_at`): `Delete`/`Restore`/`ForceDelete`/`WithTrashed`/`OnlyTrashed`
- **Lifecycle events / observers**: override hooks (`creating`/`created`, `updating`/`updated`,
  `saving`/`saved`, `deleting`/`deleted`, `restoring`/`restored`, `forceDeleting`/`forceDeleted`,
  `retrieved`) — a *before* hook returning `.F.` cancels the operation
- **Opt-in auto-timestamps** (`created_at` / `updated_at`)
- **Trilingual EN / PT / ES** API (every verb has English, Portuguese and Spanish aliases)

## Build and run

Set the three required env vars, then run the script:

```bat
set HB_BIN=C:\harbour64\bin\hbmk2.exe
set OPENADS_LIB=C:\openads-release          :: dir with ace64.dll + ace64.lib
set OPENADS_INC=C:\openads-src\include      :: dir containing openads\ace.h
build_run.bat
```

`MSVC_SETUP` is optional: set it to the path of `vcvars64.bat` if
your shell is not already an MSVC x64 Developer Prompt.

## Connection URI schemes (via the engine)

| URI prefix | Backend |
|------------|---------|
| `sqlite://filename.db` | SQLite passthrough via ace64.dll |
| `dbf://path/to/dir` | Local DBF/NTX/CDX navigational |
| `tcp://host:port/database` | Remote OpenADS server |

## Roadmap

- External observer registry (decoupled listeners)
- Global / local query scopes
- Connection pooling
- `withPivot` · lazy nested relations
- DDL for PostgreSQL / MariaDB / ODBC dialects
- Real PostgreSQL navigational read + seek
- Test-coverage hardening

## Note

The upstream project (hb_orm2) additionally ships a direct in-process
driver that bypasses the engine for maximum speed.
