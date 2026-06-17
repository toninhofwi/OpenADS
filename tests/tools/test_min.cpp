#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
int main() {
    ADSHANDLE h=0;
    UNSIGNED32 rc=AdsConnect60((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_LOCAL_SERVER,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    printf("connect rc=%u h=%llu\n",rc,(unsigned long long)h); fflush(stdout);
    if(rc) return 1;
    ADSHANDLE hc=0;
    rc=AdsExecuteSQLDirect(h,(UNSIGNED8*)"SELECT NAME FROM system.objects WHERE OBJECT_TYPE=8",&hc);
    printf("sql1 rc=%u hc=%llu\n",rc,(unsigned long long)hc); fflush(stdout);
    if(hc) AdsCloseTable(hc); hc=0;
    rc=AdsExecuteSQLDirect(h,(UNSIGNED8*)"SELECT NAME, USER_TYPE FROM system.users ORDER BY NAME",&hc);
    printf("sql2 rc=%u hc=%llu\n",rc,(unsigned long long)hc); fflush(stdout);
    if(hc) AdsCloseTable(hc);
    AdsDisconnect(h);
    return 0;
}
