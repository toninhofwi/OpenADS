#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
int main() {
    ADSHANDLE h=0, hc=0;
    UNSIGNED32 rc;
    rc=AdsConnect60((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_LOCAL_SERVER,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    if(rc) rc=AdsConnect60((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_REMOTE_SERVER,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    printf("connect rc=%u\n",rc); fflush(stdout);
    if(rc) return 1;

    const char* queries[] = {
        "SELECT NAME FROM system.users",
        "SELECT NAME FROM system.usergroups",
        "SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers",
        nullptr
    };
    for(int i=0; queries[i]; i++) {
        hc=0;
        printf("Q: %s\n",queries[i]); fflush(stdout);
        rc=AdsExecuteSQLDirect(h,(UNSIGNED8*)queries[i],&hc);
        printf("  rc=%u hc=%llu\n",rc,(unsigned long long)hc); fflush(stdout);
        if(hc) { AdsCloseTable(hc); hc=0; }
    }
    AdsDisconnect(h);
    printf("done\n");
    return 0;
}
