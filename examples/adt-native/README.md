# Native ADT / ADI / ADM sample

**by glokcode**

Minimal Harbour console app showing OpenADS native table support end-to-end:

1. `DbCreate` → `.adt` + `.adm` (memo)
2. `INDEX ON` → `.adi`
3. `DbAppend` / field write
4. `dbSeek` on the ADI tag

## Files

| File | Role |
|------|------|
| `adt_native_demo.prg` | Demo program |
| `adt_native_demo.hbp` | hbmk2 build script (x64) |
| `build.cmd` | Windows wrapper |

## Build & run

```cmd
:: MSVC x64 Developer Command Prompt, from this directory:
build.cmd "C:\OpenADS\build\default\src\Release"
adt_native_demo.exe
```

Prerequisites: Harbour 3.2 with `contrib/rddads` (msvc64) and a built OpenADS
(`ace64.dll` + `ace64.lib`).

## Expected output (abridged)

```
OpenADS native ADT / ADI / ADM demo
by glokcode
Records: 2
Seek 'Bruno Costa': found rec 2
  obs=[memo linha 2] qtd=7
Done.
```