# ADS (SAP) vs OpenADS — heavy engine bench

This is the **primary** compatibility/performance test: same Harbour workload, same
data directory, two engines swapped at runtime:

| Engine | DLL | Role |
|--------|-----|------|
| **SAP Advantage** | `ace64.dll` (+ `adsloc64.dll` local) | Reference / production baseline |
| **OpenADS** | `openace64.dll` | Drop-in replacement under test |

The light NAV SQL backends (sqlite / postgresql / odbc-remote) measure
`AdsOpenTable` over **external** databases. This bench measures the **native
DBF/CDX engine path** — SEEK, AOF, SQL-on-DBF, append/concurrency — where OpenADS
must match SAP ADS behaviour and throughput.

## Scope (heavy)

| Workload | What it proves |
|----------|----------------|
| `seek_eq` | CDX index seek hot path (200× `dbSeek`) |
| `aof_eq` | Rushmore-style filter walk (`AdsSetAOF`) |
| `rdd_scan_eq` | Full table scan baseline |
| `sql_count` / `sql_fetch` | SQL engine on `.dbf` (OpenADS `AdsExecuteSQLDirect`) |
| `openads_stress` | Generate 100k+ row synthetic DBF + indexes |
| `openads_concurrency_stress` | Multi-thread append/lock/read on one table |

The harness here is an **A/B runner**: it runs the same SEEK/AOF/SQL suite twice,
once with `openace64.dll` and once with SAP `ace64.dll` beside the exe, and diffs
the medians per workload.

## Measured cycle

1. **Prepare** — `openads_bench` or `openads_stress` creates `bench.dbf` + `bench.cdx`
   (deterministic seed; default 10k–100k rows).
2. **Run A** — copy SAP `ace64.dll` (+ `adsloc64.dll`) → execute workloads → JSON/log.
3. **Run B** — copy OpenADS `openace64.dll` → same data dir → same workloads.
4. **Compare** — median ms per workload; flag regressions > threshold (e.g. 1.5×).

No PII. Synthetic data only. Portable paths via env (no hardcoded drive letters).

## Environment variables

| Variable | Purpose |
|----------|---------|
| `OPENADS_ADS_SAP_SDK_DIR` | Advantage SDK support files (collate, icu `.dat`, cfg) |
| `HB_INSTALL` / `OPENADS_HB_BIN` | Harbour tree (`ace64.lib` in `lib/` for `hbmk2 -lrddads -lace64`) |
| `OPENADS_ADS_SAP_DLL_DIR` | Folder with SAP `ace64.dll` runtime (FWH `samples/misc` or Advantage install) |
| `OPENADS_ADS_OPEN_DLL_DIR` | Folder with `openace64.dll` |
| `OPENADS_ADS_DATA_DIR` | Shared DBF directory (default `tools/bench/fixtures/ads_compare_data`) |
| `OPENADS_ADS_ROWS` | Row count (default 10000) |
| `OPENADS_ADS_REPEATS` | Median repeats (default 3) |
| `OPENADS_HB_BIN` | `hbmk2.exe` for Harbour build |

**Harbour / SAP link:** `<harbour>/lib/ace64.lib` (`contrib/rddads` — same as `hbmk2 -lace64`).
**SAP runtime DLL:** `ace64.dll` beside the exe (typical: FiveWin `FWH*/samples/misc`, or your Advantage redistribute). The Advantage SDK supplies collate/icu cfg and `ace32` only — not `ace64.dll`.
**OpenADS:** `openace64.dll` / `openace64.lib` from the OpenADS build.

## How to run

```powershell
pwsh tools/scripts/prepare_ads_compare_data.ps1 -Rows 2000 -Build
pwsh tools/scripts/run_ads_vs_openads_bench.ps1 -Rows 2000 -Repeats 3
```

Data is created with SAP `ace64` (`ads_prepare_sap_64.exe`). Each engine builds its
own `bench.cdx` (CDX formats are not interchangeable). RDD workloads run by default;
SQL-on-DBF runs on OpenADS only (SAP local SQL rejects `bench.dbf` link syntax).

Produces:

- `tools/bench/results/ads_vs_openads_latest.json`
- `tools/bench/results/OPENADS_ADS_VS_OPENADS_FWH.pdf`

## Success criteria

- Both engines complete all workloads without ABI errors.
- OpenADS within agreed ratio of SAP ADS per workload (project-specific SLA).
- Results committed on the PR branch when the SAP DLL path is available in CI/dev.

## Relation to NAV bench

| Suite | Path | Backend |
|-------|------|---------|
| NAV bench (PR #26) | `demo_nav_multidb.prg` | Remote SQL URIs (sqlite/pg/odbc) |
| **ADS vs OpenADS** | `ads_compare` engine A/B + stress tools | Local DBF/CDX engine |

Both are required for a complete OpenADS Plus + engine validation story.