# `complete/` — every back-end, one CRUD cycle, an auditable benchmark

This is the capstone of the ORM track. A single program
([`all_backends_crud_bench.prg`](all_backends_crud_bench.prg)) opens
**every back-end you have configured**, runs the **same** ORM CRUD cycle
on each, and finishes with a benchmark you can re-run and verify.

The Harbour code is identical for all back-ends — only the connection
string changes. That is the whole point: the companion ORM gives you one
API over SQLite, DBF, PostgreSQL, MariaDB, ODBC and a remote `tcp://`
server.

## What it does, per back-end

1. Create a fresh `people` table and **insert `N` invented rows** (timed).
2. **`SELECT` everything** (timed) and compute a content **checksum**.
3. **Find `K` rows by primary key** (timed) — the seek-vs-scan number.
4. **Update `K` rows** by primary key (timed).
5. **Delete `K` rows** by primary key (timed).
6. Verify every deleted key is **no longer findable** (`del ok`).

Then it prints the numbers as **CSV** plus a short **summary**.

## Why it is "auditable"

Two independent checks turn the run from "some numbers scrolled by" into
something you can trust:

- **Content checksum.** The seed data is fully deterministic, and the
  checksum is order-independent (a sum, not a sequence). So *every*
  back-end must produce the **same** checksum. If one disagrees you have
  found a real data-correctness bug — the program says so and exits
  non-zero.
- **Delete invariant.** After deleting `K` keys, each must be
  unfindable. `Model:Find` honours deletion on **both** paths (the SQL
  `WHERE`, and the navigational scan), so this check is uniform and
  reliable across back-ends.

> The post-delete row count is checked with `Find`, **not** with a raw
> `SELECT COUNT` over the navigational (DBF) table. SQL-over-DBF does not
> honour the deletion flag, so a raw count would still see the
> logically-deleted rows. That is a documented engine quirk, not an ORM
> error — `Model:Find`/`Delete` take the navigational path, which is
> correct.

## Reading the benchmark — "seek vs scan"

The headline is the **`find/op(ms)`** column. The *same*
`Model:Find( id )` call takes two different routes — and which route
depends on how the companion ORM **routes** the back-end, not on the
database product alone:

| Path | Back-ends | How a primary-key lookup runs | Cost vs table size |
|------|-----------|-------------------------------|--------------------|
| **seek** | SQLite (the only SQL-routed back-end) | the ORM emits a parametric `SELECT` and the engine resolves the key with an **index** | roughly flat |
| **scan** | DBF and the remote `tcp://` server | the ORM **walks** the cursor, honouring the deletion flag row by row | grows with `N` |

> **Important — do not misread the numbers.** The stable companion ORM
> takes the SQL path **only** for the local SQLite back-end. Every other
> back-end is driven through the navigational cursor ABI, so it **scans**.
> In this stack the PostgreSQL / MariaDB / ODBC back-ends are exposed as
> **read-only navigational bridges** over a live SQL table: the ORM can
> OPEN and READ a server table by cursor, but it does not CREATE / write
> through them, so this bench's write+SQL cycle runs end to end only on
> SQLite, DBF and the `tcp://` server. Point a `DEMO_*_URI` at a
> pre-seeded server table and the read path works while the schema/write
> steps report `SKIP`.

A representative local run (`N=500`, `K=100`):

```
backend     path    find/op(ms)   del ok
sqlite      seek          0.040      yes
dbf         scan          3.660      yes
remote-tcp  scan         21.370      yes
```

Same call, large difference — and the gap widens as the table grows,
because a scan is `O(n)` while an indexed seek is `O(log n)`. Raise
`BENCH_N` and watch the navigational numbers climb while `sqlite` stays put.

> The navigational scan is **on purpose**: it guarantees correct
> deletion semantics without a SQL server. The engine itself now also
> supports an `O(log n)` indexed seek on navigational tables; a future
> ORM revision can adopt it for the navigational path.

## CSV columns

```
backend,nav,rows,operation,total_ms,per_op_ms,ops
```

| Column | Meaning |
|--------|---------|
| `backend` | which database (`sqlite`, `dbf`, `postgresql`, …) |
| `nav` | `yes` if this is the navigational (scan) path |
| `rows` | `N`, the rows inserted |
| `operation` | `insert` / `select_all` / `find_pk` / `update_pk` / `delete_pk` |
| `total_ms` | wall-clock for the whole operation |
| `per_op_ms` | `total_ms / ops` (blank for `select_all`) |
| `ops` | how many operations were timed |

Paste it into a spreadsheet, re-run, and compare — the timings are
machine-dependent but the **shape** (seek flat, scan rising) and the
**checksums** are not.

## Tunables (environment variables)

| Variable | Default | Purpose |
|----------|---------|---------|
| `BENCH_N` | `500` | rows inserted per back-end |
| `BENCH_K` | `100` | keyed find / update / delete operations |
| `BENCH_SQLITE_URI` | `sqlite://./_bench.db` | override the built-in SQLite file |
| `BENCH_DBF_DIR` | `./_bench_dbf` | override the built-in DBF folder |
| `DEMO_PG_URI` | *(off)* | `postgresql://user:pass@host:5432/db` |
| `DEMO_MARIA_URI` | *(off)* | `mariadb://user:pass@host:3306/db` |
| `DEMO_ODBC_URI` | *(off)* | `odbc://Driver={...};Server=...;Database=...` |
| `DEMO_REMOTE_URI` | *(off)* | `tcp://host:6262/` (an OpenADS server's data dir) |

SQLite and DBF run out of the box. The SQL servers and the remote
back-end switch on **only** when you point their `DEMO_*_URI` at a
reachable server — otherwise they are skipped cleanly (the program never
fails just because a server is absent). See
[`../../docs/connection-strings.md`](../../docs/connection-strings.md)
for the exact URI formats and
[`../../docs/local-and-remote.md`](../../docs/local-and-remote.md) for
the remote server.

## Build & run

Like the other ORM examples, this one needs the **companion ORM
sources** plus an OpenADS DLL that routes `sqlite://`. Use the generic
[`../build.cmd`](../build.cmd):

```cmd
:: from a developer command prompt, in the orm/ folder:
set OADS_ORM_SRC=C:\path\to\the\orm\src
set OPENADS_INCDIR=C:\path\to\OpenADS\include
build.cmd complete\all_backends_crud_bench.prg  C:\path\to\folder-with-the-DLL
complete\all_backends_crud_bench.exe
```

The POSIX variant and the full flag list are in
[`../../docs/building-and-running.md`](../../docs/building-and-running.md).

To exercise a heavier run and watch the scan cost grow:

```cmd
set BENCH_N=5000
set BENCH_K=300
complete\all_backends_crud_bench.exe
```
