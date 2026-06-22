# ORM track -- advanced: the two doors (next-revision ORM)

The per-back-end examples (`../sqlite`, `../dbf`, ...) cover everyday CRUD
plus the fluent `:ToSql()` builder. This folder exercises the **next
revision** of the companion Harbour ORM, which adds two "doors" into the
same engine -- both shown end to end in [`two_doors.prg`](two_doors.prg),
over SQLite so the output is identical on any machine:

* **Door A -- DB-first ("connect and use").** Point the ORM at an
  *existing* table; it introspects the columns and their types and hands
  you a working model **without writing a class** (`ORM_Abrir`). It can
  also **scaffold** a model `.prg` from the live schema (`ORM_Scaffold`).

* **Door B -- full power.** A broad fluent builder (joins, aggregates,
  group/having, limit/offset) whose SQL you can **inspect before it
  runs** (`:Compiled()`), plus model **relations** with **eager loading**
  that avoids the classic N+1 query explosion -- proven here with a live
  query counter (1 base query + 1 for all related rows, not N+1).

Verbs come in English and Portuguese (`Find`/`Buscar`, `All`/`Todos`,
`With`/`Com`, `Where`/`Onde`); the example mixes them on purpose.

## Build & run

Unlike the stable examples, this one is compiled against the
**next-revision** ORM sources, so the builder is pulled from there:

```bat
rem  <example.prg>      the example, e.g. two_doors.prg
rem  <openads-lib-dir>  folder with the OpenADS DLL + import lib
rem
rem  set OADS_ORM_SRC    -> the next-revision ORM's src\ folder
rem  set OPENADS_INCDIR  -> folder holding openads\ace.h
build.cmd two_doors.prg <openads-lib-dir>
```

`build.cmd` discovers whichever ORM source files exist, so it adapts to
the revision you point `OADS_ORM_SRC` at. The connection string defaults
to a local SQLite file (`sqlite://./demo_advanced.db`); override it with
the `DEMO_DB` environment variable. Data is invented (users + posts).
