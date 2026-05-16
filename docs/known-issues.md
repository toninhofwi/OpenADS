---
title: Known issues
layout: default
nav_order: 9
---

# Known issues — current

Status as of **v1.0.0-rc25** (2026-05-16). The historical M3-era
compat-breaking CDX / NTX list that lived here is closed and now
only survives in `git log`.

## Open

### X# `AXDBFCDX` RDD — minor gaps

The full `tests/smoke/xsharp/AdsSmoke.prg` + `AdsSmoke_remote.prg`
flow passes (rc19), but a handful of `ADSRDD.prg` entry points
still resolve to `AE_FUNCTION_NOT_AVAILABLE` so the X# runtime
falls back to its own client path. The X# RDD keeps working — but
applications that explicitly depend on these calls will hit the
not-available path.

- `AdsEval*Expr` family — server-side expression evaluation
  helpers used by `ADSRDD.prg`'s server-side query path. The
  client-side fallback handles every common case.
- A handful of `AdsStmt*` helpers used by the X# SQL surface.
- The RI / unique / autoinc enforcement *toggles* are
  accept-and-ignore (the underlying enforcement still happens
  through `AdsCreateIndex` / DD).

See [rddads / X# RDD compat](en/rddads-compat/) for the full
list of versioned overloads and what each one does.

### Wire-protocol forward-only prefetch

`M12.21` prefetch (forward-only row look-ahead on `Skip(+N)`)
was disabled in **M12.21b** (rc9) after cursor-drift
regressions on indexed scans. The wire still benefits from the
row cache (M12.17), nav-ack trailer (M12.18), and record-count
cache (M12.19), but speculative read-ahead is currently off.

### Studio LocalServer auth

LocalServer mode (the Studio embedded in `ace64.dll` /
`ace32.dll`) currently has no HTTP Basic auth. The default bind
host is `127.0.0.1`, so a desktop app does not silently expose
its data dir to the LAN — but if you set
`OPENADS_STUDIO_HOST=0.0.0.0`, put the console behind a reverse
proxy that handles authentication. Remote Server mode
(`openads_serverd`) supports `--http-user user:password`.

### Studio HTTPS

The embedded HTTP console (cpp-httplib) only ships TLS via
OpenSSL, which isn't bundled in the daemon to keep the release
binary lean. Terminate HTTPS in front with Caddy / nginx /
stunnel / SSH tunnel — see [TLS deployment](en/tls-deployment/)
for the recipes. A dedicated `OPENADS_WITH_OPENSSL=ON` CMake
option is on the roadmap.

## Closed in v1.0.0-rc1 .. rc25

See `CHANGELOG.md` for the full per-release breakdown. The big
ones since the M3-era list:

- **M3 CDX / NTX compat-breaking issues** — all closed by
  `M3.6` .. `M3.10` (`bBits` formula, compound structure-tag,
  branch descent endian / offset, multi-tag-per-file, multi-
  level NTX, descending / unique round-trip, deleted records
  excluded from `AdsCreateIndex`).
- **TLS** — shipped in v0.4.0 (M12.12 / M12.13).
- **AOF / Rushmore** — shipped in v1.0.0-rc12; `AdsSetAOF` no
  longer fails on non-optimisable expressions since rc21.
- **X# `AXDBFCDX` RDD compatibility** — shipped in
  v1.0.0-rc19 (M12.22 / M12.23), full local + remote.
- **DBF header last-update date** — shipped in rc21 (M12.24)
  and rc22 (M12.25 stamps on create).
- **Embedded Studio** (LocalServer mode) — shipped in
  v1.0.0-rc9.
- **Wire perf (~30× xbrowse repaint)** — shipped in rc18 via
  M12.17..M12.20.
- **Windows Service + systemd / launchd units** — shipped in
  rc14.
- **`AdsMg*` server telemetry** — shipped in v1.0.0-rc24; the
  ~17 management functions report real figures instead of
  zero-fill stubs.
- **Index correctness** — shipped in v1.0.0-rc25:
  `AdsCreateIndex61` decoded the wrong option bit (built every
  CDX / NTX tag descending), `ALIAS->FIELD` index keys were
  unparseable, and not-positioned reads returned generic 5000
  instead of `AE_NO_CURRENT_RECORD` (5026).

## Reporting

File issues at
[github.com/FiveTechSoft/OpenADS/issues](https://github.com/FiveTechSoft/OpenADS/issues).
For X# RDD specifics, include the `ADSRDD.prg` entry point that
returned `AE_FUNCTION_NOT_AVAILABLE` and the exact call site —
that's what drove the M12.22 / M12.23 batches.
