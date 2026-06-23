# ORM example over the OpenADS ACE ABI

Self-contained ActiveRecord + query-builder ORM (hb_orm2) running
entirely on top of OpenADS' `ace64.dll`.  The demo connects via
`sqlite://` so every call lands on the engine; the same code works
with `dbf://` (local DBF directory) and `tcp://` (OpenADS server).

## What it shows

- Schema / migrations: `CreateTable` with typed columns
- Model CRUD: `Create`, `Find`, `Set`, `Save`
- Fluent query builder: `Where`, `OrderBy`, `Get`, aggregates (`Sum`, `Count`)
- Relations + eager loading with `with()` — anti-N+1 proven by query counter
- Bilingual PT/EN API (`Find`/`Buscar`, `Save`/`Salvar`, `All`/`Todos`, …)

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

## Note

The upstream project (hb_orm2) additionally ships a direct in-process
driver that bypasses the engine for maximum speed.
