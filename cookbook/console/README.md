# Console track

Pure Harbour against OpenADS — **no ORM**. Just the stock xBase verbs
(`USE` / `INDEX ON` / `dbAppend` / `dbSeek` / `dbSkip` / `PACK` …) landing
on the OpenADS engine via the `ADSCDX` RDD. Start here if you want to see
the engine working at the lowest level.

Each file is heavily commented and self-contained. Read them in order:

| Example | Level | Shows |
|---------|-------|-------|
| [`01_hello_table`](01_hello_table.prg) | simple | connect to a local folder, create a table, build an index, append rows, walk in index order, exact `dbSeek` |
| [`02_index_seek`](02_index_seek.prg) | intermediate | several tags in one `.cdx`, switching the active order, exact vs. soft seek, a range walk |
| [`03_transactions`](03_transactions.prg) | intermediate | `AdsBeginTransaction` / commit / rollback around a batch of writes |
| [`04_dbf_maintenance`](04_dbf_maintenance.prg) | intermediate | `DELETE` (mark) / `SET DELETED` / `RECALL` / `PACK`, with `UPPER()` keys |

## Build & run

No drive letters are baked in — the build scripts read everything from
the environment. From a developer prompt in this folder:

```cmd
:: point at the OpenADS DLL + import lib, then:
build.cmd 01_hello_table.hbp  C:\path\to\folder-with-the-DLL
01_hello_table.exe
```

(`build.sh` is the POSIX equivalent.) Full toolchain details are in
[`../docs/building-and-running.md`](../docs/building-and-running.md).

## Engine version

These examples exercise index ordering, `dbSeek`, and `PACK` — areas that
have seen recent engine improvements. For correct output, build against a
current engine (the repository's `main` plus any in-flight engine fixes).
The example **code** is idiomatic and matches the native `DBFCDX`
semantics, so it stays correct as the engine catches up.
