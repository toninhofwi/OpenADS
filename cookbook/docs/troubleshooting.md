# Troubleshooting

Common problems when building or running the examples, with the likely
cause and the fix. Most of these are link-time or run-time wiring issues,
not bugs in your code.

## Symptom -> cause -> fix

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `AdsVersion()` / engine version looks like a high number (e.g. `12.x`) instead of a low `0.0a`-style string | You loaded a different ACE-compatible DLL, not OpenADS | Fix PATH ordering so the OpenADS DLL resolves first; check which one is found (`where ace64.dll` on Windows; `ldd` / inspect `LD_LIBRARY_PATH` on POSIX) |
| `unresolved external symbol AdsConnect60` (or any `Ads*`) at link | `OPENADS_LIB` not set, or the wrong import lib for your toolchain | Pass the OpenADS library folder to the build script; make sure `OPENADS_ACELIB` matches the build (`ace64` vs `openace64`) and your toolchain |
| `lib 'rddads' not found` | The `rddads` contrib RDD was not built for the compiler you selected | Build / install `rddads` for that compiler (the same `-comp` / `-cpu` you pass to `hbmk2`) |
| Runtime: "DLL not found" | The OpenADS DLL is not next to the `.exe` and not on `PATH` / `LD_LIBRARY_PATH` | Put `OPENADS_LIB` on `PATH` (Windows) or `LD_LIBRARY_PATH` (POSIX), or copy the DLL next to the `.exe` |
| `BASE/1003 rddads/<n>` at startup | Missing `REQUEST ADS, ADSCDX, ADSNTX` -- the `#include` alone does not register the RDD | Add `REQUEST ADS, ADSCDX, ADSNTX` to your program |
| `unresolved __imp_modf` / `__imp_rand` / `LNK4217` / `LNK4286` at link | C-runtime vintage mismatch (prebuilt libs target an older CRT) | Add the CRT-compat flags: `/NODEFAULTLIB:libucrt -lucrt -lvcruntime -llegacy_stdio_definitions` |
| Connecting with `sqlite://` / `postgresql://` / `mariadb://` / `odbc://` fails with a "backend disabled" error | The DLL was built without that back-end's flag | Rebuild with the matching `OPENADS_WITH_<X>` flag, or use a DLL that already has it; the examples treat this as "skip this back-end" |
| Strings appear truncated in a grid | `CHARACTER` columns are fixed-width and space-padded | Trim only for display with `AllTrim()`; the stored value keeps its full width (this is not lost data) |

## A few extra notes

- **Always check which DLL you loaded first.** The version-number check is
  the quickest way to catch a wrong-engine load. A high version number is
  the tell.
- **Linking vs. registering.** `AdsConnect60` unresolved is a *link*
  problem; `BASE/1003 rddads` is a *registration* problem
  (`REQUEST ADS, ...`). They look similar but the fixes are different.
- **"backend disabled" is expected sometimes.** A DLL built without, say,
  PostgreSQL support is fine -- the connect call returns a clear error
  instead of crashing, and the examples just skip that back-end.

## See also

- [`building-and-running.md`](building-and-running.md) -- prerequisites,
  link line, and the run-time DLL resolution rules.
- [`connection-strings.md`](connection-strings.md) -- the URI schemes and
  their `OPENADS_WITH_<X>` build flags.
- [`local-and-remote.md`](local-and-remote.md) -- in-process vs. remote.
- [`field-types.md`](field-types.md) -- on the fixed-width `CHARACTER`
  behaviour.
- [`../README.md`](../README.md) -- the high-level tour.
