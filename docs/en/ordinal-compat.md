---
title: Fix "ordinal NNN not found"
layout: default
parent: Home (EN)
nav_order: 8
permalink: /en/ordinal-compat/
---

# Fix "the ordinal NNN could not be located in ace64.dll"

## Symptom

After dropping OpenADS' `ace64.dll` (or `ace32.dll`) onto an
existing application's `PATH`, Windows pops:

> **The procedure entry point at ordinal 328 could not be
> located in the dynamic link library `ace64.dll`.**

The number changes (`328`, `415`, ...). The application aborts.

## Root cause

The application or its `rddads.lib` was linked against an
**`ace32.lib` / `ace64.lib` import library that records imports
by ordinal**, not by name. Each `Ads*` entry point lives at a
specific ordinal (1, 2, 3, …, 328, …) inside the SAP-shipped
DLL. The Windows loader looks up that ordinal in the new
DLL — and fails, because OpenADS' upstream `.def` file only
declares exports by name, leaving ordinals to be auto-assigned
in source-file order. The numbers don't line up with SAP's.

OpenADS ships a clean-room implementation of every `Ads*`
function — the **names** are public (Harbour's `contrib/rddads`
calls them by name). What's missing for ordinal-linked
binaries is just the **numeric mapping** from ordinal → name.
That mapping lives in the SAP-shipped DLL the user already
legally possesses; we never read SAP-owned source. A small
helper extracts the mapping locally on the user's machine.

## Fix — one-time, on the same Windows box that has SAP's DLL

### 1. Dump SAP's export table

Open a *Developer Command Prompt* and run:

```bat
dumpbin /exports ace64.dll > ace64-exports.txt
```

The output starts like:

```
        ordinal hint RVA      name
              1    0 00001234 AdsConnect60 = AdsConnect60
              2    1 0000125A AdsDisconnect = AdsDisconnect
            ...
            328  ... ........ AdsXyzWhatever = AdsXyzWhatever
```

### 2. Convert it into an OpenADS `.def` file

```bat
python tools\scripts\sap_ordinals_to_def.py ace64-exports.txt > openads_ace_ordinals.def
```

The script reads only the `<ordinal> <hint> <RVA> <name>`
columns. The names round-trip back into the OpenADS source —
those are the same public Harbour-callable names. The
ordinals are user-supplied metadata. **No SAP code is read or
copied.**

### 3. Rebuild OpenADS with the custom `.def`

```bat
cmake -S . -B build\ord -DOPENADS_ACE_DEF=%CD%\openads_ace_ordinals.def
cmake --build build\ord --target openads_ace --config Release
```

The resulting `build\ord\src\Release\ace64.dll` keeps every
function name we already implement AND assigns each one the
ordinal SAP picked. Drop it on the application's `PATH`
ahead of the original DLL — the loader's ordinal lookups
now hit the right symbol.

## Why we don't ship `ace64-exports.txt` upstream

The export table is metadata about a binary OpenADS does not
ship and does not own. Re-publishing it inside this repo would
mean redistributing data extracted from a SAP-owned binary,
which conflicts with the project's clean-room policy. Each
user generates their own copy locally from a binary they
already have a legal right to inspect.

## Won't there be ordinal drift across SAP versions?

Yes. The mapping you extract from `ace64.dll` 11.10 may differ
from 11.30 or 12.0. Re-run the two-step extract whenever the
target environment upgrades.

## Compile-from-source path (recommended when feasible)

If the consuming application can be re-linked, **don't bother
with the ordinal stunt.** Generate an import library from
OpenADS' own DLL (which uses by-name exports):

```bat
lib /def:openads_ace.def /machine:x64 /out:ace64.lib
```

…and re-link `rddads` and the application against that
`ace64.lib`. The result links by name, no ordinal coupling at
all. This is the only future-proof approach.
