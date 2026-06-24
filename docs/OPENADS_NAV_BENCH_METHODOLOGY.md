# OpenADS NAV bench — methodology (ADO/FWH bridge)

Harbour console harness measuring latency of **TOpenAdsConnection + AdsOpenTable**
(SKIP/GETFIELD navigation), not the native DBF C++ stress tool (`openads_stress`).

Scripts are **portable**: repo-relative paths; no hardcoded drive letters.
Optional overrides via environment variables (see table below).

## Coverage status

| Backend | Runner | In PR #26 | 30-cycle result |
|---------|--------|-----------|-----------------|
| SQLite | `run_nav_bench_sqlite.ps1` | Yes | 30/30 pass |
| PostgreSQL | `run_nav_bench_pg.ps1` | Yes | 30/30 pass |
| ODBC (Firebird fixture) | `run_nav_bench_odbc.ps1` | Yes | 30/30 pass |
| SQL Server (ODBC) | `run_nav_bench_mssql.ps1` | Runner added | Needs SQL Server + seed |
| MariaDB | `run_nav_bench_maria.ps1` | Runner added | Needs MariaDB on :3306 |
| **ADS vs OpenADS (engine)** | `run_ads_vs_openads_bench.ps1` | Planned | See separate doc |

The **heavy** SAP ADS vs OpenADS engine comparison (SEEK/AOF/SQL on local DBF,
100k+ rows, concurrency) is documented in
`docs/OPENADS_ADS_VS_OPENADS_BENCH.md` — not the same as remote NAV SQL backends.

## Scope

| Item | Value |
|------|-------|
| Program | `examples/fivewin/demo_nav_multidb.prg` (bench mode) |
| Class | `TOpenAdsConnection` → `OpenAds_AdoTryBridge` |
| ABI | `OADS_Connect60`, `AdsOpenTable`, `AdsGotoTop`, `AdsSkip`, `AdsGetField` |
| SQLite fixture | `tools/bench/fixtures/nav_clientes.db` (3 rows Ana/Bob/Cid) |
| PG fixture | `seed_nav_clientes_pg.sql` via `OPENADS_TEST_PG_URI` |
| ODBC fixture | `.fdb` file via `OPENADS_ODBC_FIXTURE` + `OPENADS_ODBC_DRIVER` |
| SQLite build | `openace64.dll` sqlpass (`-DOPENADS_WITH_SQLITE=ON`) |
| PG build | `openace64.dll` postgresql (`-DOPENADS_WITH_POSTGRESQL=ON`) |
| ODBC build | `openace64.dll` odbc (`-DOPENADS_WITH_ODBC=ON`) |
| Harbour link | **no** `rddads` (avoids `ace64` vs `openace64` split) |

## Environment variables (optional)

| Variable | Purpose |
|----------|---------|
| `OPENADS_REPO_ROOT` | OpenADS clone root (default: auto-detect) |
| `OPENADS_WORKTREE_ROOT` | Parent folder of sibling worktrees (sqlpass/odbc/pg) |
| `OPENADS_DLL_SRC` | `build/.../src` folder containing `openace64.dll` |
| `OPENADS_HB_BIN` | Path to `hbmk2.exe` |
| `OPENADS_MSVC_SETUP` | MSVC `setup_x64.bat` (compile) |
| `OPENADS_MSVC_BIN` | Folder with `vcruntime140.dll` (runtime) |
| `OPENADS_TEST_SQLITE_URI` | `sqlite://...` URI |
| `OPENADS_TEST_PG_URI` | `postgresql://...` URI |
| `OPENADS_PSQL_BIN` | Folder containing `psql.exe` |
| `OPENADS_ODBC_FIXTURE` | Path to Firebird `.fdb` |
| `OPENADS_ODBC_DRIVER` | Registered ODBC driver name |
| `OPENADS_DOC_GLOBAL_DIR` | If set, copy PDF to an external folder |
| `FIREBIRD` | Firebird bin folder (ODBC only) |

## Measured cycle (one iteration)

1. **connect** — `OpenAds_AdoTryBridge` / `OADS_Connect60`
2. **nav** — `AdsOpenTable` + validate 3 rows (Ana / Bob NULL / Cid + EOF)
3. **close** — `AdsCloseTable` + `OADS_Disconnect` (included in `total_ms`)

Warm-up: `OPENADS_NAV_BENCH_WARMUP` discarded iterations (default 1).

## Metrics

| Field | Meaning |
|-------|---------|
| `connect_ms` | backend handshake + registration |
| `nav_ms` | open + walk + field reads |
| `total_ms` | connect + nav + disconnect |
| `avg_*`, `min_*`, `max_*` | over post warm-up iterations |
| `p50_total_ms`, `p95_total_ms` | percentiles on `total_ms` |

Parseable stdout lines: `BENCH_ROW,...` and `BENCH_SUMMARY,...`.

## How to run

```cmd
cd examples\fivewin
demo_nav_bench.bat sqlite 30 1
demo_nav_bench.bat pg 30 1
demo_nav_bench.bat odbc 30 1
```

Or PowerShell (seed + JSON + PDF in repo):

```powershell
pwsh tools/scripts/run_nav_bench_sqlite.ps1 -Iters 30
pwsh tools/scripts/run_nav_bench_pg.ps1 -Iters 30
pwsh tools/scripts/run_nav_bench_odbc.ps1 -Iters 30 -Fixture <path.fdb>
pwsh tools/scripts/run_nav_bench_mssql.ps1 -Iters 30
pwsh tools/scripts/run_nav_bench_maria.ps1 -Iters 30
```

## Artifacts (in repo)

| File | Backend |
|------|---------|
| `tools/bench/results/nav_bench_sqlite_latest.json` | SQLite |
| `tools/bench/results/nav_bench_pg_latest.json` | PostgreSQL |
| `tools/bench/results/nav_bench_odbc_latest.json` | ODBC |
| `tools/bench/results/OPENADS_NAV_BENCH_*_FWH.pdf` | PDF per backend |
| `tools/scripts/gen_nav_bench_pdf.py` | Builds PDF from JSON |

## Success criteria

- `fail=0` in `BENCH_SUMMARY`
- PDF generated under `tools/bench/results/`
- Results committed on the PR branch