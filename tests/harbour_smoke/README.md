# Harbour rddads smoke test (M8.1)

This directory contains a minimal Harbour `.prg` that links against
Harbour's `contrib/rddads` static library and the OpenADS-shipped
`ace64.dll` / `ace64.lib`. The point is to validate **end-to-end** that
every ACE entry point Harbour expects is resolvable from OpenADS.

## What we proved

`c:\harbour\lib\win\msvc64\rddads.lib` references **225 distinct `Ads*`
entry points**. After M8.1, OpenADS's `ace64.dll` exports all of them:
80 real implementations (M0-M7) + 146 stubs that return
`AE_FUNCTION_NOT_AVAILABLE` (5004) so the link succeeds.

Linking `smoke.prg` against `rddads.lib` + `ace64.lib` produces a clean
resolution of every `HB_FUN_ADSVERSION`/`AdsGetVersion`/etc. symbol
chain.

## Known limitation: Harbour-side CRT mismatch

`smoke.exe` does **not** finish linking on the current host. The
remaining 3 unresolved externals are CRT functions that Harbour's
prebuilt `msvc64` libs were compiled against:

    __imp__dclass    referenced in hbcommon.lib(hbprintf.obj)
    __imp__dsign     referenced in hbcommon.lib(hbprintf.obj)
    __imp__wfsopen   referenced in hbcommon.lib(hbfopen.obj)

These are legacy MSVC 2013-era runtime symbols that disappeared when
Microsoft split the C runtime into `ucrt` + `vcruntime` in VS2015.
This is a **Harbour build flag issue**, not an OpenADS issue:
re-building Harbour `contrib/hbcommon` with `-DHB_LEGACY_LEVEL5 = 0`
or with `legacy_stdio_definitions.lib` on the link line resolves them.

A clean smoke that produces a runnable `smoke.exe` lands in **M8.2**
once a Harbour build matching the host VS toolchain is available.

## Running

```cmd
:: From the OpenADS root:
cmake --build build\default --config Release
cd tests\harbour_smoke
run_build.bat
```

`run_build.bat`:
1. Calls `vcvars64.bat` to bring the MSVC link toolchain into PATH.
2. Puts `c:\harbour\bin\win\msvc64` and the OpenADS Release output on
   PATH so `hbmk2` and `ace64.dll` are found.
3. Invokes `hbmk2 -comp=msvc64 -lrddads -L<openads-out> -lace64`.
