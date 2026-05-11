# X# RDD smoke test

`AdsSmoke.prg` is a headless console app that exercises OpenADS' ACE
DLL through X#'s **Advantage RDD** (`AXDBFCDX`). The RDD layer
P/Invokes `ace32.dll` / `ace64.dll`, so running this against an
OpenADS-built DLL is an end-to-end check that the entry points X#
binds — including the versioned overloads (`AdsCreateTable90`,
`AdsOpenTable90`, `AdsCreateIndex90`, …) — resolve and behave.

Exit code `0` = pass; non-zero = the first failed assertion.

## Prerequisites

- The **X# compiler + runtime** — install from <https://www.xsharp.eu/>
  (the X# build tools or the IDE). Not vendored here.
- A built OpenADS `ace64.dll` (x64) or `ace32.dll` (x86), e.g.
  `build/default/src/Release/ace64.dll`. **It must be the OpenADS
  DLL, not SAP's** — put its directory first on `PATH`.

## Build & run

```cmd
:: from this directory, with the X# build tools on PATH and
:: OpenADS' ace64.dll dir prepended to PATH
set PATH=C:\OpenADS\build\default\src\Release;%PATH%
xsc AdsSmoke.prg /r:XSharp.Core.dll /r:XSharp.RT.dll /r:XSharp.RDD.dll /out:AdsSmoke.exe
AdsSmoke.exe
echo EXIT=%ERRORLEVEL%
```

…or open `AdsSmoke.xsproj` in the X# IDE / `msbuild AdsSmoke.xsproj`.

> The exact compiler invocation and assembly references vary by X#
> release; treat the above as a starting point and adjust to your
> installed version. The `.prg` itself uses only core RDD verbs
> (`DbCreate` / `OrdCreate` / `DbAppend` / `DbSeek` / …).

## What it does

1. `RddSetDefault("AXDBFCDX")`, `DbCreate` a 4-field table (incl. an
   `M` memo field) in a temp dir.
2. `OrdCreate` two tags (`NAME`, `CITY`) in the same `.cdx`.
3. `DbAppend` four rows; `FieldPut` each column incl. the memo;
   `DbCommit`. Check `LastRec()` / `RecCount()`.
4. NAME order: `GoTop` → Alice, `Skip ±1` walks the order, `GoBottom`
   → Diana, `Skip +1` → `Eof()`.
5. `DbSeek` a hit (`"Charlie"` → Valencia) and a miss (`"Zzz"` →
   `! Found()`).
6. Memo round-trip: re-seek and check each row's `NOTE`.
7. `DbDelete` / `Deleted()` / `DbRecall`.
8. Replace a key field (Diana's CITY → `"Bilbao"`), `DbCommit`,
   `OrdSetFocus("CITY")`, `GoTop` → Barcelona, `DbSeek("Bilbao")` →
   Diana — verifies the index is maintained on a replace.
9. `DbCloseArea`.

## `AdsSmoke_remote.prg` — remote variant

Drives the *same* X# `AXDBFCDX` RDD against a remote `openads_serverd`
over the wire instead of local files:

```
AdsConnect60("tcp://host:port/<datadir>", ADS_REMOTE_SERVER, NULL, NULL, 0, OUT hConn)
AX_SetConnectionHandle(hConn)        // parks it in CoreDb's RDD info
RddSetDefault("AXDBFCDX")
DbUseArea(TRUE, "AXDBFCDX", "customer", ...)   // → AdsOpenTable90(hConn, ...)
```

It opens an *existing* table (`customer.dbf`) — it doesn't `DbCreate`,
since `AdsCreateTable` doesn't route remotely yet — and does read/nav
(`RecCount` / `GoTop` / `Skip ±` / `GoBottom` / `Eof` / `FieldGet`).
Server URI: env var `OPENADS_XS_REMOTE`, else the documented dev box.
Build it the same way; run with OpenADS' `ace64.dll` on PATH.

## Status (M12.23)

Both harnesses **pass end-to-end** against OpenADS' `ace64.dll` —
local (`AdsSmoke.prg`) and remote-over-the-wire (`AdsSmoke_remote.prg`).

Getting here closed: ~46 missing ACE exports; `AdsAppendRecord` not
auto-locking the new record (X#'s `GoHot` requires it); an option-bit
bug where `ADS_COMPOUND` (always set by X#'s ADSRDD for CDX) was
misread as `ADS_DESCENDING` so every order built reversed;
`AdsCreateTable` not staging the `.fpt` for tables with an `M` field;
and three remote-path gaps (`remote_field_index` ordinal-as-pointer
AV, the remote `AdsOpenTable` `.dbf` default, `AdsGetTableFilename`
missing a remote branch).
