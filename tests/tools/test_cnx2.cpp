#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Load ace64 by ordinal like test_connect.exe
typedef unsigned int       UNSIGNED32;
typedef unsigned long long ADSHANDLE;
typedef unsigned char      UNSIGNED8;
typedef unsigned short     UNSIGNED16;

#define ADS_LOCAL_SERVER  1

extern "C" {
typedef UNSIGNED32 (__stdcall *PFN_Connect)(UNSIGNED8* path, UNSIGNED16 server,
    UNSIGNED8* user, UNSIGNED8* pw, UNSIGNED32 opts, ADSHANDLE* ph);
typedef UNSIGNED32 (__stdcall *PFN_ExecSQL)(ADSHANDLE h, UNSIGNED8* sql, ADSHANDLE* phc);
typedef UNSIGNED32 (__stdcall *PFN_Close)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_Disc)(ADSHANDLE h);
}

int main() {
    HMODULE hDll = LoadLibraryA("ace64.dll");
    if (!hDll) { printf("LoadLibrary failed\n"); return 1; }

    auto pfnConn = (PFN_Connect)  GetProcAddress(hDll, (LPCSTR)315);
    auto pfnSQL  = (PFN_ExecSQL)  GetProcAddress(hDll, (LPCSTR)265);
    auto pfnDisc = (PFN_Disc)     GetProcAddress(hDll, (LPCSTR)32);
    auto pfnClose= (PFN_Close)    GetProcAddress(hDll, (LPCSTR)19);

    if (!pfnConn||!pfnSQL||!pfnDisc||!pfnClose) {
        printf("GetProcAddress failed\n"); return 1;
    }

    ADSHANDLE h=0, hc=0;
    UNSIGNED32 rc = pfnConn((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_LOCAL_SERVER, (UNSIGNED8*)"adssys", (UNSIGNED8*)"pmsys", 0, &h);
    printf("connect rc=%u h=%llu\n", rc, (unsigned long long)h); fflush(stdout);
    if (rc) return 1;

    rc = pfnSQL(h, (UNSIGNED8*)"SELECT NAME FROM system.objects WHERE OBJECT_TYPE=8", &hc);
    printf("sql rc=%u hc=%llu\n", rc, (unsigned long long)hc); fflush(stdout);
    if (hc) pfnClose(hc);

    pfnDisc(h);
    printf("done\n");
    return 0;
}
