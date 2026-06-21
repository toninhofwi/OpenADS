// Shims for MSVC2013-era CRT entry points referenced by Harbour's
// pre-built msvc64 libs (hbcommon.lib in particular). These symbols
// were dropped when Microsoft split the C runtime into UCRT +
// vcruntime in VS2015, so a modern host toolchain can't satisfy them.
// Re-exporting them from ace64.dll lets a Harbour app link cleanly
// without rebuilding Harbour itself.
//
// The shims are exposed under OpenADS-prefixed names; openads_ace.def
// then aliases the legacy names (_dclass, _dsign) onto these
// implementations so the public DLL exports match what hbcommon.lib
// expects. _wfsopen is NOT shimmed — modern UCRT provides it natively
// and exporting it caused a circular symbol resolution crash.
// (Reported by JONSSON RUSSI, RusSoft Ltda.)
//
// Whole file is Windows-only; on POSIX builds the DLL is built
// without these shims (no Harbour-MSVC2013 link concern there).

#ifdef _WIN32

#include <cmath>
#include <conio.h>
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <share.h>

extern "C" {

int openads_dclass(double x) {
    return std::fpclassify(x);
}

int openads_dsign(double x) {
    return std::signbit(x) ? 1 : 0;
}

// Console / fd helpers used by Harbour's gtstd terminal driver.
int openads_getch (void)            { return ::_getch(); }
int openads_kbhit (void)            { return ::_kbhit(); }
int openads_eof   (int fd)          { return ::_eof(fd); }

} // extern "C"

#endif // _WIN32
