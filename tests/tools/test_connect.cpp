#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
int main(int argc, char** argv) {
    const char* path = argc>1?argv[1]:"E:\\AdsData\\saa\\mp.add";
    const char* pw   = argc>2?argv[2]:"mp10";
    // Try different server types
    const int types[] = { ADS_LOCAL_SERVER, ADS_REMOTE_SERVER, ADS_AIS_SERVER };
    const char* tnames[] = { "LOCAL", "REMOTE", "AIS" };
    for (int i=0; i<3; i++) {
        ADSHANDLE h = 0;
        UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)path, types[i],
                                      (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &h);
        printf("%-8s rc=%u %s\n", tnames[i], rc, rc==0?"OK":"FAILED");
        if (rc==0) {
            // Get user list
            ADSHANDLE hCur=0;
            rc=AdsExecuteSQLDirect(h,(UNSIGNED8*)"SELECT NAME FROM system.objects WHERE OBJECT_TYPE=8",&hCur);
            printf("  users query rc=%u\n", rc);
            if (hCur) AdsCloseTable(hCur);
            AdsDisconnect(h);
        }
    }
    return 0;
}
