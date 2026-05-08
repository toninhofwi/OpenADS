---
title: Benchmarks
layout: default
parent: Home (EN)
nav_order: 5
permalink: /en/benchmarks/
---

# Benchmarks

`tools/bench/openads_bench` generates a synthetic 100 000-row
DBF (`ID N(8,0)`, `TAG C(4)`, `AMT N(8,2)`) and times a fixed
set of SQL workloads through the public ABI
(`AdsExecuteSQLDirect`). Median of 5 repeats per workload,
`Release` builds.

## v0.4.x results (2026-05-06)

| Workload (median ms)   | Windows MSVC | Linux clang -O3 | macOS AppleClang |
|------------------------|-------------:|----------------:|-----------------:|
| stage 100 k-row DBF    |        63.5  |          57.9   |           34.0   |
| `SELECT COUNT(*)`      |       297.7  |          42.0   |          103.9   |
| `WHERE TAG = 'AAAA'`   |       303.7  |          48.3   |          108.4   |
| `SUM/AVG/MIN/MAX(AMT)` |       374.3  |         120.5   |          136.1   |
| `GROUP BY TAG`         |       321.9  |          58.6   |          120.9   |
| `ORDER BY AMT LIMIT 10`|       668.0  |         165.4   |          260.5   |
| `DISTINCT TAG`         |       598.4  |          95.2   |          213.4   |
| `BETWEEN 100 AND 500`  |       314.1  |          63.7   |          114.4   |

Linux clang -O3 wins every SQL workload — roughly 7× faster
than MSVC Release on the full-table count, 4× on the heavier
`ORDER BY`. macOS Intel sits between the two.

## Bench v2 — index-aware workloads (Windows MSVC, 100 k rows)

| Workload (median ms)  | ms |
|-----------------------|---:|
| `CREATE INDEX ID_IDX` | 38.0 |
| `WHERE ID = 50000` (post-index)        | 308.0 |
| `WHERE ID BETWEEN 10000 AND 20000`     | 308.2 |
| `UNION ALL` of two filtered selects    | 608.2 |
| `GROUP BY TAG HAVING COUNT(*) > 100`   | 0.2 |

The `indexed_eq` row at ~308 ms ≈ `seq_walk_where` at ~315 ms
exposes a known opportunity: the SQL planner currently does
not push WHERE predicates into a matching CDX/NTX index.
Closing that gap is a future milestone.

## Bench v3 — AOF (Rushmore-style) results (rc12, 100 k rows)

`AdsSetAOF` parses + evaluates the cond, installs a per-record
bitmap as a filter predicate that `Skip` / `GoTop` honour, and
routes individual leaves through CDX / NTX index range scans
whenever an open index has the leaf's field as its key
expression. `AdsGetAOFOptLevel` reports `ADS_OPTIMIZED_FULL` /
`PART` / `NONE` based on per-leaf coverage. Sparse-bitmap
navigation (M-AOF.5) lifts the visible-set walk from O(N) to
O(M).

Cross-platform numbers, all on the same 100 000-row
synthetic DBF, all medians of 5 repeats, all `Release` builds:

| AOF workload                                      | Win MSVC x64 | Linux clang -O3 | macOS AppleClang | OptLevel |
|---------------------------------------------------|-------------:|----------------:|-----------------:|----------|
| `AdsSetAOF("TAG='AAAA'")`, no TAG index            |   593 ms     |     93 ms       |    210 ms        | NONE     |
| `AdsSetAOF("TAG='AAAA'")`, TAG indexed             |   323 ms     |     58 ms       |    119 ms        | FULL     |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`, idx  |    24 ms     |    4.5 ms       |      9 ms        | FULL     |

Speedups vs the unindexed full-scan baseline on the same host:

| AOF workload                                      | Win MSVC | Linux clang | macOS |
|---------------------------------------------------|---------:|------------:|------:|
| `AdsSetAOF("TAG='AAAA'")`, TAG indexed             |   1.83×  |    1.61×    | 1.77× |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`       |  24.4×   |   20.7×     | 23.4× |

What drives the speedup:

1. `AdsSetAOF` itself becomes a range scan over the matching
   index instead of a full record decode + AST eval per row.
   That alone is the rc11 (M-AOF.4) gain.
2. `Skip` / `GoTop` walk only the matching records (sparse-bitmap
   navigation, M-AOF.5) instead of iterating every recno asking
   the predicate. That's the rc12 gain — the textbook Rushmore
   "10-100×" speedup window for selective filters.

The eq-walk speedup at ~1.83× is bounded by the per-visible-
record `load_record_` decode cost (~80 µs × 3848 matching
records ≈ 310 ms floor on Windows). Applications that don't
touch the matched data — for example `COUNT(*)` over the AOF
set, or `dbSeek`-style point lookups — hit the full Rushmore
window on top of the range-scan gain.

## Run it on your hardware

```sh
cmake --build build/default --target openads_bench --config Release
./build/default/tools/bench/openads_bench --rows 100000 --repeats 5 --csv
```

The `--csv` flag emits one CSV row per workload so results pipe
straight into a spreadsheet.

## Methodology notes

- Each workload runs N times; min / median / max are reported.
- `stage_dbf` is the time to write the synthetic DBF; reported
  once (no repeats).
- All SQL workloads run after the same in-process `AdsConnect60`
  so warm-up cost is amortised.
- `run_query` walks the first 10 rows of the result cursor so
  the time captures materialisation, not just plan compilation.
