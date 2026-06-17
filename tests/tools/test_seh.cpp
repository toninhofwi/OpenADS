#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <vector>
#include <string>

int main() {
    ADSHANDLE h=0, hc=0;
    UNSIGNED32 rc;
    rc=AdsConnect60((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_LOCAL_SERVER,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    printf("connect rc=%u h=%llu\n",rc,(unsigned long long)h); fflush(stdout);
    if(rc) return 1;

    const char* sql = "SELECT NAME FROM system.objects WHERE OBJECT_TYPE=8";
    printf("Q: %s\n", sql); fflush(stdout);

    __try {
        rc = AdsExecuteSQLDirect(h,(UNSIGNED8*)sql,&hc);
        printf("  rc=%u hc=%llu\n",rc,(unsigned long long)hc); fflush(stdout);
        if (hc) AdsCloseTable(hc);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        printf("  EXCEPTION 0x%08X caught!\n", code); fflush(stdout);
        // Get exception info
        EXCEPTION_RECORD er = {};
        // Can't easily get the fault address here without EXCEPTION_POINTERS
        printf("  (exception in AdsExecuteSQLDirect)\n"); fflush(stdout);
    }

    AdsDisconnect(h);
    printf("done\n");
    return 0;
}
