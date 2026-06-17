#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Just the types we need from ace.h, without including it
typedef unsigned int  UNSIGNED32;
typedef unsigned short UNSIGNED16;
typedef unsigned char  UNSIGNED8;
typedef UNSIGNED32     ADSHANDLE;

#define ADS_LOCAL_SERVER  1
#define ADS_REMOTE_SERVER 2

typedef UNSIGNED32 (WINAPI *FN_Connect60)(UNSIGNED8*,UNSIGNED16,UNSIGNED8*,UNSIGNED8*,UNSIGNED32,ADSHANDLE*);
typedef UNSIGNED32 (WINAPI *FN_Disconnect)(ADSHANDLE);
typedef UNSIGNED32 (WINAPI *FN_GetUserProp)(ADSHANDLE,UNSIGNED8*,UNSIGNED16,void*,UNSIGNED16*);

int main(int argc, char** argv) {
    const char* dll_path   = argc>1?argv[1]:"adsloc64.dll";
    const char* add_path   = argc>2?argv[2]:"E:\\AdsData\\saa\\mp.add";
    const char* adssys_pw  = argc>3?argv[3]:"mp10";
    const char* target_user= argc>4?argv[4]:"mpuser";

    printf("Loading: %s\n", dll_path); fflush(stdout);
    HMODULE dll = LoadLibraryA(dll_path);
    if (!dll) { printf("LoadLibrary failed: %u\n", GetLastError()); return 1; }
    printf("Loaded OK\n"); fflush(stdout);

    auto fn_connect = (FN_Connect60)GetProcAddress(dll, "AdsConnect60");
    auto fn_disc    = (FN_Disconnect)GetProcAddress(dll, "AdsDisconnect");
    auto fn_getusr  = (FN_GetUserProp)GetProcAddress(dll, "AdsDDGetUserProperty");
    printf("AdsConnect60:           %p\n", (void*)fn_connect);
    printf("AdsDisconnect:          %p\n", (void*)fn_disc);
    printf("AdsDDGetUserProperty:   %p\n", (void*)fn_getusr);
    fflush(stdout);
    if (!fn_connect || !fn_disc || !fn_getusr) {
        printf("GetProcAddress failed - trying ace64.dll\n");
        FreeLibrary(dll);
        dll = LoadLibraryA("ace64.dll");
        if (!dll) { printf("ace64.dll load failed: %u\n", GetLastError()); return 1; }
        fn_connect = (FN_Connect60)GetProcAddress(dll, "AdsConnect60");
        fn_disc    = (FN_Disconnect)GetProcAddress(dll, "AdsDisconnect");
        fn_getusr  = (FN_GetUserProp)GetProcAddress(dll, "AdsDDGetUserProperty");
        if (!fn_connect || !fn_disc || !fn_getusr) {
            printf("Still failed\n"); FreeLibrary(dll); return 1;
        }
    }

    printf("DD:  %s\n", add_path);
    printf("User: %s\n\n", target_user);

    // Try LOCAL
    ADSHANDLE hConn = 0;
    printf("Trying ADS_LOCAL_SERVER...\n"); fflush(stdout);
    UNSIGNED32 rc = fn_connect((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    printf("LOCAL rc=%u hConn=%llu\n", rc, (unsigned long long)hConn); fflush(stdout);

    if (rc != 0) {
        printf("Trying ADS_REMOTE_SERVER...\n"); fflush(stdout);
        rc = fn_connect((UNSIGNED8*)add_path, ADS_REMOTE_SERVER,
                        (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
        printf("REMOTE rc=%u hConn=%llu\n", rc, (unsigned long long)hConn); fflush(stdout);
    }
    if (rc != 0) { printf("All connect attempts failed\n"); FreeLibrary(dll); return 1; }

    // Query all users with property 1102
    const char* users[] = { "user-admin", "user-backup", "user-debug", "user-public", "root", nullptr };
    if (argc > 4) {
        unsigned char buf[4096] = {}; UNSIGNED16 len = sizeof(buf);
        rc = fn_getusr(hConn, (UNSIGNED8*)target_user, 1102, buf, &len);
        printf("  %-20s rc=%u groups=[%.*s]\n", target_user, rc, (int)len, buf);
    } else {
        for (int i = 0; users[i]; i++) {
            unsigned char buf[4096] = {}; UNSIGNED16 len = sizeof(buf);
            rc = fn_getusr(hConn, (UNSIGNED8*)users[i], 1102, buf, &len);
            if (rc == 0)
                printf("  %-20s groups=[%.*s]\n", users[i], (int)len, buf);
            else
                printf("  %-20s rc=%u\n", users[i], rc);
            fflush(stdout);
        }
    }

    fn_disc(hConn);
    FreeLibrary(dll);
    return 0;
}
