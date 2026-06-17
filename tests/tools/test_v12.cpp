#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <vector>
#include <string>
#include <array>
#include <algorithm>

// Load ACE functions dynamically from a named DLL
typedef UNSIGNED32 (WINAPI *FN_Connect60)(UNSIGNED8*,UNSIGNED16,UNSIGNED8*,UNSIGNED8*,UNSIGNED32,ADSHANDLE*);
typedef UNSIGNED32 (WINAPI *FN_Disconnect)(ADSHANDLE);
typedef UNSIGNED32 (WINAPI *FN_GetUserProp)(ADSHANDLE,UNSIGNED8*,UNSIGNED16,VOID*,UNSIGNED16*);

int main(int argc, char** argv) {
    const char* dll_path   = argc>1?argv[1]:"ace64_v12.dll";
    const char* add_path   = argc>2?argv[2]:"E:\\AdsData\\saa\\mp.add";
    const char* adssys_pw  = argc>3?argv[3]:"mp10";
    const char* target_user= argc>4?argv[4]:"mpuser";

    HMODULE dll = LoadLibraryA(dll_path);
    if (!dll) { printf("LoadLibrary failed: %u\n", GetLastError()); return 1; }
    auto fn_connect = (FN_Connect60)GetProcAddress(dll, "AdsConnect60");
    auto fn_disc    = (FN_Disconnect)GetProcAddress(dll, "AdsDisconnect");
    auto fn_getusr  = (FN_GetUserProp)GetProcAddress(dll, "AdsDDGetUserProperty");
    if (!fn_connect || !fn_disc || !fn_getusr) {
        printf("GetProcAddress failed\n"); FreeLibrary(dll); return 1;
    }

    printf("DLL: %s\n", dll_path);
    printf("DD:  %s\n\n", add_path);
    fflush(stdout);

    // Try LOCAL then REMOTE
    ADSHANDLE hConn = 0;
    printf("Trying LOCAL...\n"); fflush(stdout);
    UNSIGNED32 rc = fn_connect((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    if (rc != 0) {
        printf("LOCAL failed (%u), trying REMOTE...\n", rc); fflush(stdout);
        rc = fn_connect((UNSIGNED8*)add_path, ADS_REMOTE_SERVER,
                        (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    }
    if (rc != 0) { printf("All connect attempts failed: %u\n", rc); FreeLibrary(dll); return 1; }
    printf("Connected (handle=%llu)\n\n", (unsigned long long)hConn);

    // Query property 1102 for target user
    unsigned char buf[4096] = {}; UNSIGNED16 len = sizeof(buf);
    rc = fn_getusr(hConn, (UNSIGNED8*)target_user, 1102, buf, &len);
    if (rc == 0) {
        printf("User %-20s groups: [%.*s]\n", target_user, (int)len, buf);
    } else {
        printf("AdsDDGetUserProperty(%s, 1102) rc=%u\n", target_user, rc);
    }

    fn_disc(hConn);
    FreeLibrary(dll);
    return 0;
}
