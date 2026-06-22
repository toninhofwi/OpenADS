# OpenADS Cookbook

A self-contained collection of **runnable, heavily-commented examples**
for the OpenADS engine — from a five-line "hello table" a newcomer can
follow, up to a multi-database CRUD application with an auditable
benchmark.

Every example here:

- **creates its own data** (invented, fictitious rows) at run time or
  from a tiny seed script, so you can clone, build, and run with nothing
  to download or prepare;
- is **portable** — no hardcoded drive letters or machine-specific
  paths; the build scripts take the location of the OpenADS library as
  an argument / environment variable;
- is **commented for two audiences** — a first-timer who just wants it
  to work, and an experienced developer who wants to adapt it.

These samples are deliberately kept **separate** from any project's
internal test data. The only data they touch is the made-up data they
create themselves.

## Two ways to talk to OpenADS

OpenADS exposes the data engine through one DLL
(`ace64.dll` / `ace32.dll`, also shipped as `openace64.dll`). You can
drive it at two levels, and this cookbook has a track for each:

| Track | Folder | Style | Best for |
|-------|--------|-------|----------|
| **Pure Harbour** | [`console/`](console/) | The stock xBase verbs (`USE`, `INDEX ON`, `dbSeek`, `dbAppend`) and the raw `Ads*` API. **No extra dependency.** | File tables (DBF / ADT) locally or against a remote server; learning the engine from the ground up. |
| **ORM** | [`orm/`](orm/) | The companion Harbour ORM — `Model` / fluent query builder / introspection. | Working across many database back-ends (file tables and SQL engines) with the same high-level code; CRUD apps; benchmarking. |
| **GUI** | [`fivewin/`](fivewin/) | A desktop GUI layer on top of the same engine. | Showing data in a browsable grid / edit form. |

You do not have to pick one — the tracks share the same engine and the
same connection strings, so it is normal to prototype in `console/`,
move data access to the ORM, and put a GUI on top.

## One engine, many back-ends

The same example code reaches very different storage by changing only
the **connection string**. The engine decides what to do from the URI
scheme:

| Back-end | Connection string (example) | Where it runs | Notes |
|----------|-----------------------------|---------------|-------|
| File tables (DBF/CDX/NTX) | a **folder path**, e.g. `C:\data` | In-process | The classic xBase file format. |
| File tables (ADT/ADI/ADM) | a **folder path** + `ADS_ADT` file type | In-process | The native typed-file format (13 field types, memo/binary). |
| SQLite | `sqlite://path/to/file.db` | In-process | Single-file SQL database. Optional `?key=...` for an encrypted file. |
| PostgreSQL | `postgresql://user:pass@host:5432/dbname` | Client → server | Also accepts `postgres://` and `pgsql://`. |
| MariaDB / MySQL | `mariadb://user:pass@host:3306/dbname` | Client → server | Also accepts `mysql://`. |
| ODBC (any ODBC-reachable engine) | `odbc://Driver={...};Server=...;Database=...;UID=...;PWD=...` | Client → server | Use this to reach SQL engines through their ODBC driver. |
| Remote OpenADS server | `tcp://host:port/path` (or `tls://...`) | Client → `openads` server | Drives file tables hosted by the server process. Application code is identical to the local path. |

See [`docs/connection-strings.md`](docs/connection-strings.md) for the
exact format of each one, and which build flag turns each back-end on.

> **Build flags.** The file-table back-ends are always available. The
> SQL back-ends are compiled in with flags such as `OPENADS_WITH_SQLITE`,
> `OPENADS_WITH_POSTGRESQL`, `OPENADS_WITH_MARIADB`, `OPENADS_WITH_ODBC`.
> If you connect with a SQL scheme to a DLL that wasn't built with that
> back-end, the connect call returns a clear error instead of crashing —
> the examples treat that as "skip this back-end" so they keep running.

## Local vs. remote

- **Local (in-process).** The engine runs inside your program; the
  "server" is the DLL itself. Fastest to set up — no daemon, no network.
  Point the connection at a folder (file tables) or a `sqlite://` file.
- **Remote.** A separate `openads` server process owns the data; your
  program is a client connecting over `tcp://` (or `tls://`). The
  application code does **not** change — only the connection string. The
  SQL back-ends (PostgreSQL / MariaDB / ODBC) are inherently
  client-to-server already.

[`docs/local-and-remote.md`](docs/local-and-remote.md) walks through
both, including how to start a server for the remote examples.

## Building & running

Each track has a generic `build.cmd` (Windows) / `build.sh` (POSIX)
that take the OpenADS library folder as an argument — no paths are baked
in. You need a Harbour install and a C toolchain. Full step-by-step,
including the common pitfalls, is in
[`docs/building-and-running.md`](docs/building-and-running.md).

Quick version (Windows, from a developer command prompt):

```cmd
cd console
build.cmd <folder-with-the-OpenADS-DLL>     :: builds 01_hello_table by default
01_hello_table.exe
```

## Folder map

```
cookbook/
  README.md                  <- you are here
  docs/                      <- the "why" and "how", in plain language
    connection-strings.md
    local-and-remote.md
    field-types.md
    building-and-running.md
    troubleshooting.md
  console/                   <- pure Harbour, no ORM (simple -> complex)
  orm/                       <- the companion ORM, one folder per back-end
    sqlite/  dbf/  adt/  postgresql/  mariadb/  odbc/
    complete/                <- opens every configured back-end: CRUD + benchmark
    seed/                    <- fictitious-data seed scripts per back-end
  fivewin/                   <- GUI examples (grid + edit form)
```

## A note on the data

All names, cities, amounts and dates in these examples are **invented**
for illustration. They are not derived from, and must not be replaced
in the committed examples by, anyone's real records. Keep the public
samples generic; keep real data in your own private tests.
