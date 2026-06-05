# Wilson — Harbour rddads NTX index test

Minimal Harbour `.prg` that opens a DBF table with **two Clipper-style NTX
indexes** through the ADS RDD in remote-server mode. Validates end-to-end
NTX index navigation (top/bottom/walk) via OpenADS' `ace64.dll`.

## What it tests

| Area | Detail |
|------|--------|
| NTX index open | `USE ejecutiv INDEX ejec1x, ejecu2x` through ADS RDD |
| OrdCount / OrdName | Both indexes visible after open |
| OrdSetFocus | Switch between indexes |
| dbGoTop / dbGoBottom | Navigate to bounds via each index |
| Ordered walk | `DO WHILE ! Eof(); dbSkip(); ENDDO` through name index |
| Remote server | `SET SERVER REMOTE` → `AdsConnect(".")` path |

## Fixture

| File | Description |
|------|-------------|
| `ejecutiv.dbf` | Sales rep table (~43 fields, C/N/D/L types) |
| `EJEC1X.ntx` | Index on `DIAS_MAS` (numeric, ascending) |
| `EJEC2X.NTX` | Index on `E_NOMBRE` (character, ascending) |

## Build

```cmd
rem From a Developer Command Prompt (vcvars64.bat already run):
build.cmd [openads_build_dir]

rem Default openads_build_dir: ..\..\build\default\src\Release
```

Prerequisites:
- Harbour 3.2+ with `contrib/rddads` at `c:\harbour`
- OpenADS built (`ace64.lib` + `ace64.dll`)
- MSVC toolchain on PATH

## Run

```cmd
rem ace64.dll must be on PATH (or in same directory):
set PATH=..\..\build\default\src\Release;%PATH%
test_open_ads_adsntx.exe
```

Expected output: walks all rows ordered by name, prints record number and
`E_NOMBRE` / `DIAS_MAS` for each.

## Relationship to other tests

- `tests/smoke/harbour/` — CDX compound-index smoke test (same toolchain,
  different index type)
- `tests/unit/ntx_*_test.cpp` — C++ doctest unit tests for the NTX engine
  (no Harbour RDD layer)
- This test sits between them: exercises NTX through the real Harbour
  `rddads` → `ace64.dll` boundary, the way a real Clipper application would.

## Why "wilson"

Named after the original Clipper developer who contributed the fixture
data. The table (`ejecutiv`) is a real-world sales force database from a
legacy Clipper application.
