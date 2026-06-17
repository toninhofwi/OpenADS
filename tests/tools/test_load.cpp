#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
typedef unsigned long long ADSHANDLE;
typedef unsigned int UNSIGNED32;
typedef unsigned char UNSIGNED8;
typedef unsigned short UNSIGNED16;
typedef UNSIGNED32 (__stdcall *PFN_Connect)(UNSIGNED8*, UNSIGNED16, UNSIGNED8*, UNSIGNED8*, UNSIGNED32, ADSHANDLE*);
typedef UNSIGNED32 (__stdcall *PFN_Disconnect)(ADSHANDLE);
int main() {
    HMODULE h = LoadLibraryA("f:\\ads11\\ace64.dll");
    printf("handle=%p\n", h);
    if (!h) { printf("Load failed: %u\n", GetLastError()); return 1; }
    char path[512]={};
    GetModuleFileNameA(h, path, 511);
    printf("loaded from: %s\n", path);
    auto fn = (PFN_Connect)GetProcAddress(h, "AdsConnect60");
    if (!fn) { printf("no AdsConnect60\n"); return 1; }
    ADSHANDLE hConn = 0;
    auto rc = fn((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add", 1, 
                 (UNSIGNED8*)"adssys", (UNSIGNED8*)"", 0, &hConn);
    printf("rc=%u hConn=%llu\n", rc, hConn);
    if (hConn) {
        auto disc = (PFN_Disconnect)GetProcAddress(h, "AdsDisconnect");
        if (disc) disc(hConn);
    }
    FreeLibrary(h);
    return 0;
}
