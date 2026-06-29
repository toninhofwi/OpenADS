# Harbour remote server + `AdsCreateIndex` over TCP

Console and FiveWin demos that connect to `openads_serverd` over TCP,
open a server-side DBF, create CDX tags with **`AdsCreateIndex`**
(delegating to `AdsCreateIndex61` on the wire), and browse the
`CCOLONIA` sample table.

The flow mirrors a typical Harbour + `rddads` remote app:

1. `AdsConnect60( "tcp://host:port/", ADS_REMOTE_SERVER, …, @hConn )`
2. `AdsConnection( hConn )`
3. `USE CCOLONIA … VIA "ADSCDX"`
4. `AdsCreateIndex( "CCOLONIA.CDX", "COLONIA", "COLONIA", "", 0 )`
5. `OrdSetFocus( "COLONIA" )` → navigate / xBrowse

> Generic engine support lives in OpenADS core (`network/remote_index_nav.*`,
> `AdsCreateIndex` → `AdsCreateIndex61` on remote tables). The Harbour
> wrapper is `tools/harbour_patch/ads_create_index.c` / `rddads-compat.patch`.

## Files

| File | Role |
|------|------|
| `colonias_common.prg` | `InicializarOpenADS()` — connect, open table, `AdsCreateIndex`, focus tag. |
| `colonias_console.prg` | Headless smoke (`colonias_console.exe`). |
| `colonias.prg` | FiveWin `xBrowse` GUI (`colonias.exe`). |
| `../../tools/harbour_patch/ads_create_index.c` | Harbour `AdsCreateIndex()` wrapper (also in `rddads-compat.patch`). |
| `colonias_console.hbp` | `hbmk2` project — console build. |
| `colonias_gui.hbp` | `hbmk2` project — FiveWin GUI build. |
| `build.cmd` | Build console demo + copy `ace64.dll`. |
| `build_gui.cmd` | Build GUI demo (needs FiveWin). |
| `build_and_run.cmd` | Build, start local `openads_serverd`, run smoke, stop server. |
| `create_data.prg` | Optional: regenerate `data/CCOLONIA.DBF` locally. |
| `data/CCOLONIA.DBF` | Sample table shipped for the server `--data` directory. |

## Prerequisites

- **Harbour 3.2** with `contrib/rddads` built for **msvc64**
  (`%HB_INSTALL%\lib\win\msvc64\rddads.lib`, default `c:\harbour`).
- **MSVC x64** (Visual Studio Developer Command Prompt).
- **Built OpenADS** — `openace64.dll` + import lib under
  `build/default/src/Release/`. Build the server too:
  `cmake --build build/default --config Release --target openads_serverd`
- **FiveWin (FWH)** — only for the GUI target; commercial, not vendored
  (`%FWDIR%`, default `c:\fwteam`). See `examples/fivewin/`.

## Quick start (console smoke)

```cmd
:: from this directory, MSVC x64 prompt:
build_and_run.cmd

:: against a remote server on your LAN:
set OADS_REMOTE_URI=tcp://192.168.100.7:6262/
colonias_console.exe
```

`build_and_run.cmd` starts `openads_serverd --port 6262 --data data\`,
runs `colonias_console.exe`, and expects exit code 0.

Equivalent manual steps:

```cmd
build.cmd C:\OpenADS\build\default\src\Release

openads_serverd --port 6262 --data %CD%\data

set OADS_REMOTE_URI=tcp://127.0.0.1:6262/
colonias_console.exe
```

## FiveWin GUI

```cmd
set FWDIR=c:\fwteam
build_gui.cmd C:\OpenADS\build\default\src\Release
colonias.exe
```

## Environment variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `OADS_REMOTE_URI` | `tcp://127.0.0.1:6262/` | `AdsConnect60` target |
| `OPENADS_LIB` | `../../build/default/src/Release` | Link dir + DLL copy source |
| `OPENADS_ROOT` | repo root (set by `.hbp`) | `ace.h` include path |
| `HB_INSTALL` | `c:\harbour` | Harbour binaries |
| `FWDIR` | `c:\fwteam` | FiveWin headers/libs (GUI only) |

## What it exercises

- TCP remote connection (`AdsConnect60` + `AdsConnection`)
- Server-side `USE` of `CCOLONIA.DBF`
- Native index creation via `AdsCreateIndex` / `AdsCreateIndex61` on the
  server (not Harbour `INDEX ON`)
- Post-create navigation (`OrdSetFocus`, `DBGoTop`, `DBSkip`)
- Optional FiveWin `xBrowse` over the remote work area