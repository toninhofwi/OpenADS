# Building and running

The examples build with generic scripts that take the OpenADS library
folder as an argument -- **no paths are baked in**. This page lists the
prerequisites, what the scripts do, the essential link line, and how to
make sure the right DLL is found at run time.

## Prerequisites

- A **Harbour** install, including the `rddads` contrib RDD.
- A **C toolchain**:
  - Windows: the MSVC x64 "Developer Command Prompt".
  - POSIX: `gcc` or `clang`.
- A built **OpenADS DLL** plus its import library, in one folder.

## What the build scripts do

Each track has a `build.cmd` (Windows) and a `build.sh` (POSIX). They take
the **OpenADS library folder** as their first argument and set two
environment variables from it:

| Variable | Meaning |
|----------|---------|
| `OPENADS_LIB` | The folder with the OpenADS DLL + import lib. |
| `OPENADS_ACELIB` | The import-lib base name: `ace64` for release builds, `openace64` for 64-bit "plus" builds. |

Because everything comes from the argument, the same script works on any
machine.

```cmd
:: Windows, from a developer command prompt
cd console
build.cmd C:\openads\lib
01_hello_table.exe
```

```sh
# POSIX
cd console
./build.sh /opt/openads/lib
./01_hello_table
```

## The essential link line

Link the ADS RDD, point at the OpenADS library, and pull in the core RDDs:

```
-lrddads -L${OPENADS_LIB} -l${OPENADS_ACELIB} -lrddcdx -lrddntx -lrddfpt
```

Select the toolchain on the `hbmk2` command line, for example:

```
hbmk2 01_hello_table.prg -comp=msvc64 -cpu=x86_64 \
      -lrddads -L${OPENADS_LIB} -l${OPENADS_ACELIB} \
      -lrddcdx -lrddntx -lrddfpt
```

> **Register the RDD.** Linking is not enough -- put
> `REQUEST ADS, ADSCDX, ADSNTX` in your program (the `#include` alone does
> not register the RDD). Forgetting this is the usual cause of a
> `BASE/1003 rddads/<n>` error at startup.

## Running with the DLL resolved first

At run time the **OpenADS DLL must be the first one found**. If another
ACE-compatible DLL is on the path ahead of it, you will load the wrong
engine. Either:

- put `OPENADS_LIB` on `PATH` (Windows) or `LD_LIBRARY_PATH` (POSIX), or
- copy the OpenADS DLL next to the produced `.exe`.

```cmd
:: Windows
set PATH=C:\openads\lib;%PATH%
01_hello_table.exe
```

```sh
# POSIX
export LD_LIBRARY_PATH=/opt/openads/lib:$LD_LIBRARY_PATH
./01_hello_table
```

You can confirm which DLL resolves with `where ace64.dll` on Windows or
`ldd` / `LD_LIBRARY_PATH` inspection on POSIX. A version string that looks
like a high number (e.g. `12.x`) instead of a low `0.0a`-style string means
you loaded a different engine, not OpenADS.

## ORM examples -- the extra bits

The ORM examples additionally compile the companion ORM's **Harbour
sources** and its **small C glue**. For those you also need:

- the **OpenADS include directory** (the header used by the C glue), and
- the same link line as above.

Some Harbour builds ship prebuilt libs that target an older C runtime. If
the link fails to resolve symbols like `__imp_modf`, add CRT-compat flags:

```
/NODEFAULTLIB:libucrt -lucrt -lvcruntime -llegacy_stdio_definitions
```

These are harmless when the runtimes already match -- they only fill in
symbols an older-vintage lib expects. See
[`troubleshooting.md`](troubleshooting.md) for the full symptom list.

## See also

- [`../README.md`](../README.md) -- the high-level tour.
- [`connection-strings.md`](connection-strings.md) -- which URI selects
  which back-end (and the matching `OPENADS_WITH_<X>` build flags).
- [`local-and-remote.md`](local-and-remote.md) -- in-process vs. remote.
- [`troubleshooting.md`](troubleshooting.md) -- when a build or run fails.
