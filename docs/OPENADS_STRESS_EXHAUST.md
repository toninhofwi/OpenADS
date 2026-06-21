# OpenADS exhaustive stress — local + server

Fundamental validation: same synthetic workloads on **two deployment paths**:

| Path | Connection | DLL / process |
|------|------------|-----------------|
| **Local** | `AdsConnect(data_dir)` | `openace64.dll` in-process (`ADS_LOCAL_SERVER`) |
| **Server** | `tcp://host:port/<data_dir>` | `openads_serverd` + client `openace64.dll` (`ADS_REMOTE_SERVER`) |

Without both paths, wire-protocol and locking bugs stay hidden until production.

## Mode matrix (Harbour `adsexhaust`)

| Mode | Local | Server | What it exercises |
|------|:-----:|:------:|-------------------|
| `init` | yes | yes | Clean `exh.dbf` + indexes |
| `read` | yes | yes | SEEK, AOF, SQL read |
| `write` | yes | yes | Append / update / delete |
| `lock` | yes | no | Single-process flock/rlock |
| `stress` | yes | yes | Hot loop (default 50 iter) |
| `dbf` | yes | no | CDX / memo / reopen / deleted |
| `tx` | yes | no | Commit / rollback |
| `rel` | yes | no | Multi-table JOIN SQL |
| `pr` | yes | yes | CI gate: init + all suites + stress 100 |
| `trylock` | yes* | no | Multi-process lock contention |

\* `trylock` optional: `-IncludeTryLock` on the runner.

## C++ engine tools (optional)

| Tool | Path | Role |
|------|------|------|
| `openads_stress` | `tools/stress` | Large synthetic DBF via ABI |
| `openads_concurrency_stress` | `tools/stress` | Multi-thread append/lock/read |

Build: `cmake --build build/release-x64 --config Release` (see `tools/scripts/build_release_windows.bat`).

## Related benches (optional flags)

| Suite | Flag | Backend |
|-------|------|---------|
| NAV SQL | `-IncludeNav` | sqlite / postgresql / odbc remote URIs |
| SAP vs OpenADS | `-IncludeEngineCompare` | Local DBF/CDX engine A/B |
| C++ stress | `-IncludeCppStress` | ABI generators |

## Environment

| Variable | Purpose |
|----------|---------|
| `OPENADS_CONNECT_URI` | Set by runner for server modes (`tcp://127.0.0.1:PORT//...`) |
| `OPENADS_ADSEXHAUST_EXE` | `adsexhaust_64.exe` (rebuild after `adsexhaust.prg` remote patch) |
| `OPENADS_SERVERD_EXE` | `openads_serverd.exe` |
| `OPENADS_DLL_SRC` / `OPENADS_ADS_OPEN_DLL_DIR` | Folder with `openace64.dll` |

## Run

```powershell
# Rebuild adsexhaust after remote-connect patch (once):
#   cd adsexhaust && build64.bat

pwsh tools/scripts/run_openads_stress_exhaust.ps1 -Profile all
pwsh tools/scripts/run_openads_stress_exhaust.ps1 -Profile local -IncludeTryLock
pwsh tools/scripts/run_openads_stress_exhaust.ps1 -Profile server -ServerPort 17262

# Full matrix (slow):
pwsh tools/scripts/run_openads_stress_exhaust.ps1 -Profile all -IncludeNav -IncludeEngineCompare -IncludeCppStress
```

Outputs:

- `tools/bench/results/openads_stress_exhaust_latest.json`
- `tools/bench/results/OPENADS_STRESS_EXHAUST_FWH.pdf`

## Success criteria

- All requested profile modes exit 0.
- Server phase skipped only when `openads_serverd` is not built (recorded in JSON).
- No PII — synthetic `exh.dbf` data only.