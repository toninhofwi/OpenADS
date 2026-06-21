# FiveWin ADO bridge patch (Class B — `TDataBase:SqlQuery`)

Routes `FW_OpenAdoConnection` / `FW_OpenRecordSet` through OpenADS ACE instead
of ADODB COM when the connection spec uses the `OPENADS,` prefix or when
`OPENADS_ADO_BRIDGE=1` is set.

## Files

| File | Role |
|------|------|
| `openads_ado_bridge.prg` | Harbour shim (`TOpenAdsConnection`, `TOpenAdsRecordSet`) |
| `adofuncs_openads.patch` | 2-hook patch for FWH `source/function/adofuncs.prg` |

## Apply to your FiveWin (FWH) tree

```cmd
cd C:\path\to\fwh
git apply C:\path\to\openads\tools\fwh_patch\adofuncs_openads.patch
```

Copy or reference `openads_ado_bridge.prg` from your app `.hbp` **before**
`adofuncs.prg` (or add to FWH `source/function/` and rebuild FiveH lib).

## Usage

```xbase
// explicit OpenADS URI (Plus MariaDB or local data dir)
oCn := FW_OpenAdoConnection( "OPENADS,mariadb://root@127.0.0.1:3306/test" )

// or array form
oCn := FW_OpenAdoConnection( { "OPENADS", "tcp://127.0.0.1:6262/mydata" } )

// global bridge via env (same as OPENADS_CONNECT_URI auto-connect)
set OPENADS_ADO_BRIDGE=1
set OPENADS_CONNECT_URI=mariadb://root@127.0.0.1:3306/test
oCn := FW_OpenAdoConnection( "." )
```

`TDataBase:SqlQuery( "SELECT ..." )` then flows: `FW_OpenRecordSet` → ACE
`AdsExecuteSQLDirect` → `RsGetRows` layout unchanged.

Non-`OPENADS` specs still use real ADODB (no silent downgrade).

## Requirements

- Harbour `rddads` linked against `openace64.lib` / `openace64.dll`
- For Plus MariaDB: `libmariadb64.dll` next to `openace64.dll`
- See `docs/PLUS_ADO_BRIDGE.md` for full RFC

## Examples (console, no FWH window)

| Script | Path | What it validates |
|--------|------|-------------------|
| SqlQuery / passthrough | `examples/fivewin/demo_ado_multidb.prg` | `TOpenAdsRecordSet` + `AdsExecuteSQLDirect` (sqlite/sqlpass) |
| NAV / USE-SKIP | `examples/fivewin/demo_nav_multidb.prg` | `TOpenAdsConnection` + `AdsOpenTable` (sqlite/postgresql/mariadb/odbc) |

```cmd
cd examples\fivewin
demo_nav_run.bat odbc
demo_nav_stress.bat odbc 5
demo_nav_bench.bat sqlite 30 1
demo_nav_bench.bat odbc 30 1
demo_nav_bench.bat pg 30 1
pwsh ..\..\tools\scripts\run_nav_bench_sqlite.ps1 -Iters 30
pwsh ..\..\tools\scripts\run_nav_bench_odbc.ps1 -Iters 30
pwsh ..\..\tools\scripts\run_nav_bench_pg.ps1 -Iters 30
demo_nav_run.bat all
```

Seed `clientes` first: `tools/scripts/seed_nav_clientes_sqlite.py`,
`seed_nav_clientes_pg.sql`, `seed_nav_clientes_maria.sql`, or an ODBC Firebird fixture.
Methodology: `docs/OPENADS_NAV_BENCH_METHODOLOGY.md`.
Firebird stores unquoted names as uppercase — the ODBC smoke opens `CLIENTES`.
The NAV console build links `openace64` glue only (no `rddads`) so ODBC/MariaDB
URIs hit the same DLL you pass on the command line.

`tools/scripts/run_harbour_nav_odbc.ps1` — same env as `run_firebird_odbc_tests.ps1`.