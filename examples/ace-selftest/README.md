# ACE self-test (standalone Harbour host)

Small, self-contained prototypes that drive **OpenADS through the ACE API**
(`openace64.dll`) directly — **no RDD layer** (`rddads`/`lrddads`). Harbour is
only the console host; all data I/O goes through `AdsCreateTable`, `AdsCreateIndex61`,
`AdsSeek`, `AdsAppendRecord`, etc.

They exercise the full create → populate → index → seek → update cycle and a
CDX growth stress scenario, so they double as a quick smoke for the writer and
the index engine. Everything is generated in the `.prg` — no external fixtures.

| Executable | Format | What it covers |
|------------|--------|----------------|
| `oa_proto_dbf.exe` | DBF + FPT + CDX | table, 3 CDX tags, 20 rows, seek, update, extra row |
| `oa_proto_adt.exe` | ADT + ADM + ADI | same flow on ADT native tables |
| `oa_stress_cdx.exe` | DBF + CDX | repeated append/reindex on one `.cdx`, watching its size |

## Build

1. Copy `build_env.local.example.bat` to `build_env.local.bat` and edit the
   paths (Harbour, MSVC, `OPENADS_INCLUDE`, `openace64.lib`, `openace64.dll`).
   `build_env.local.bat` is git-ignored.
2. `check_ace.bat` — sanity check (ACE sources, not RDD).
3. `build.bat` — compiles `oa_proto_dbf.exe` + `oa_proto_adt.exe` (+ smoke).

## Run

```
run_dbf.bat                 # local DBF/CDX
run_adt.bat                 # local ADT/ADI
run_stress_cdx.bat data_stress 100 50 reuse
```

### Remote (against `openads_serverd`)

The default ACE wire port is **6262**. Start the server, then:

```
run_dbf.bat data remote tcp://127.0.0.1:6262/
```

## Note — cross-engine interop

These prototypes validate **self-consistency** (OpenADS reads back what OpenADS
wrote). They do **not** open the generated files with an independent native
reader, so they cannot catch on-disk format divergences on their own. Pairing
the generated `.dbf`/`.cdx`/`.fpt` with a native DBFCDX/DBFNTX reader (a Harbour
build with the standard RDD) turns this into a full cross-engine interop test.

## Glue

`oa_ace.prg` holds the C glue (`#pragma BEGINDUMP`) that maps the `OAA_*`
Harbour functions onto the ACE API. The other `.prg` files are pure Harbour.
