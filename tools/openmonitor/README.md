# openmonitor

Terminal and web monitor for a running **openads server** (`openads_serverd`).

Reads live telemetry from:

- **Wire** — `MgRequest` / `MgSnapshot` (uptime, connections, open tables, packet counters)
- **HTTP** — Studio API (`/api/health`, `/api/server/sessions`, kill session)

Useful when developing Harbour / OpenADS clients over `tcp://` or debugging a local server instance.

## Build

Requires [Rust](https://rustup.rs/) (`cargo` on `PATH`). On Windows MSVC link tools are recommended for the static CRT build (Visual Studio Build Tools or `vcvars64.bat`).

```bat
cd tools\openmonitor
build.bat
```

```sh
cd tools/openmonitor
cargo build --release
```

Binary: `target/release/openmonitor` (`.exe` on Windows).

## Usage

Start `openads_serverd` first, then:

```text
openmonitor --wire-port 16262 --http http://127.0.0.1:16263
```

One-shot snapshot (scripts / CI):

```text
openmonitor --wire-port 16262 --http http://127.0.0.1:16263 --once
```

Web dashboard (default bind `127.0.0.1:9850`):

```text
openmonitor --wire-port 16262 --http http://127.0.0.1:16263 --web
```

### Common flags

| Flag | Default | Description |
|------|---------|-------------|
| `--wire-host` | `127.0.0.1` | openads server wire host |
| `--wire-port` | `16262` | openads server wire port |
| `--http` | — | Studio HTTP base URL (enables session list + kill in TUI) |
| `--web` | off | Serve browser dashboard |
| `--web-port` | `9850` | Web UI port |
| `--once` | off | Print one snapshot and exit |
| `--interval` | `2` | TUI refresh seconds |

### TUI keys

| Key | Action |
|-----|--------|
| `q` / `Esc` | Quit |
| `r` | Refresh now |
| `k` | Kill selected HTTP session (requires `--http`) |

## Wireshark

With the server listening on the wire port:

```
tcp.port == 16262
```

## Related

Full local lab scripts (server launchers, optional Harbour probe) live in the companion repo [openads-wire-lab](https://github.com/Admnwk/openads-wire-lab).