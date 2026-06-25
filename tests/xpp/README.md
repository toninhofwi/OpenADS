# Xbase++ (Alaska) raw-ACE smoke test

*Traducciones: [Español](README.es.md) · [Português](README.pt.md)*

`test_ads.prg` is a headless Xbase++ app that drives OpenADS' ACE DLL
through the **raw ACE C API**, called dynamically with
`DllPrepareCall` / `DllExecuteCall`. It does **not** go through
Alaska's `ADSDBE` database engine (see *Why not ADSDBE* below), so it
is a direct check that OpenADS' exported cdecl ABI resolves and behaves
when called from Xbase++.

Each step is written to `test_result.log`; the last line is
`EXIT 0` on success or `EXIT 1` at the first failed assertion.
(Xbase++ console output `?` goes to a PM window, not stdout, so the
test logs to a file instead.)

## Prerequisites

- **Alaska Xbase++ 2.0** — compiler `xpp.exe` + linker `alink.exe`
  (32-bit), default at `C:\Program Files (x86)\Alaska Software\xpp20`.
  Not vendored here. The build tools need a valid product key; a trial
  build license is activated with
  `workbench20\asac.exe /p <ProductKey>` then `asac.exe /a`. Without
  it the compiler fails `XBLM707` and the linker fails `ALK5001`.
- A built OpenADS **32-bit** DLL dropped in next to the test as
  `openace32.dll`
  (`build/release-x86/src/Release/openace32.dll`). **It must be the
  OpenADS DLL, not SAP/Alaska's `ace32.dll`.** Build it with:

  ```sh
  cmake --build build/release-x86 --config Release --target openads_ace
  ```

## Build & run

```sh
# from this directory; run.sh sets the Alaska toolchain on PATH,
# compiles, links, runs headless with a timeout, and dumps the log.
bash run.sh
```

`run.sh` also takes an optional DLL-swap argument for control runs:

| command            | effect                                            |
|--------------------|---------------------------------------------------|
| `bash run.sh`      | use whatever `openace32.dll` is present           |
| `bash run.sh openads` | drop `ace32_openads.dll` in as `ace32.dll`     |
| `bash run.sh real` | drop Alaska's real `ace32.dll` in (control)       |

(The swap targets `ace32.dll`; the raw-ACE test itself loads
`openace32.dll` by name, so the swap only matters for ADSDBE-style
experiments.)

## What it does

1. `DllPrepareCall` one typed template per export (cdecl, UINT32
   return, `DLL_CALLMODE_COPY` for `@`byref outputs, `RESTOREFPU`).
2. `AdsConnect60` to the local data dir (no server).
3. `AdsCreateTable` a CDX table `ID,N,8,0;NAME,C,20,0;`.
4. `AdsAppendRecord` ×5; set fields with `AdsSetString` (numeric `ID`
   set as text — `set_field` coerces, which avoids passing a C
   `double` through `AdsSetDouble`). Check `AdsGetRecordCount` == 5.
5. `AdsCreateIndex61` a CDX tag `NAME_TAG` on `NAME`.
6. `AdsSeek "Charlie"` (string key, soft seek); read back `RecNo`
   via `AdsGetRecordNum` and `ID` via `AdsGetLong`; assert `recno==3`
   and `ID==3`.
7. `AdsCloseTable` / `AdsDisconnect`.

## Gotchas (baked into the test as comments)

- **`ADSHANDLE` is `uint64_t` — even in the 32-bit DLL.** A handle
  passed *by value* (`hConn` / `hTable` / `hIndex` inputs, and the
  `@`byref outputs) occupies **two** 32-bit stack slots. Marshaling it
  as `UINT32` shifts every following argument by 4 bytes — the symptom
  was `AdsCreateTable` failing with `"no fields"` because its field
  string landed in the wrong slot. All handles use `DLL_TYPE_INT64`.
  Functions that take a handle only *by pointer* (e.g. `AdsConnect60`'s
  `phConnect`) are unaffected — that is why connect worked while create
  did not.
- **`DllCall()` silently drops arguments past ~8.** The 10-arg
  `AdsCreateTable` lost its trailing args, so the test uses the typed
  `DllPrepareCall` / `DllExecuteCall` path instead.
- **ACE constants** (`ADS_LOCAL_SERVER`, `ADS_CDX`, `ADS_STRINGKEY`, …)
  are `#define`d locally. Xbase++'s `ads.ch` ships only the `AX_*`
  command layer, not the raw `Ads*` declarations or their constants.

## Why not ADSDBE

Xbase++ has a native ADS RDD — the **ADSDBE** DatabaseEngine
(`DbeLoad("ADSDBE")` + standard `USE` / `INDEX` / `DbSeek`). It would
be the more idiomatic integration path, but it is unusable headless
here:

- Alaska's real `ace32.dll` hangs at `DbCreate` (the interactive
  Advantage Local Server / evaluation dialog).
- OpenADS' `ace32.dll` hangs *inside* `DbeLoad` — `adsdbe.dll` calls
  an init export during engine load that blocks (a bare
  `LoadLibrary` of the DLL via `dllinfo.exe` is fine, so it is not the
  OpenADS `DllMain`).

Both hang on a GUI dialog with no stdout, which does not survive an
unattended run. The raw-ACE path bypasses `adsdbe.dll` and the local
server entirely. For an RDD-layer integration test, see the X# RDD
smoke (`tests/smoke/xsharp/`), which drives OpenADS through X#'s
`AXDBFCDX` RDD.
