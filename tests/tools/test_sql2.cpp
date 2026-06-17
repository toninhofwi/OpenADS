#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>

static void q(ADSHANDLE hConn, const char* sql) {
    printf("Q: %s\n", sql); fflush(stdout);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rc = AdsExecuteSQLDirect(hConn, (UNSIGNED8*)sql, &hCur);
    printf("  rc=%u hCur=%llu\n", rc, (unsigned long long)hCur); fflush(stdout);
    if (rc != 0) { if (hCur) AdsCloseTable(hCur); return; }
    if (!hCur) return;

    AdsGotoTop(hCur);
    UNSIGNED16 bEOF = 0;
    AdsAtEOF(hCur, &bEOF);
    int rows = 0;
    while (!bEOF && rows < 30) {
        char f1[256]={}, f2[256]={};
        UNSIGNED32 l1=255, l2=255;
        AdsGetField(hCur, (UNSIGNED8*)"1", (UNSIGNED8*)f1, &l1, ADS_NONE);
        AdsGetField(hCur, (UNSIGNED8*)"2", (UNSIGNED8*)f2, &l2, ADS_NONE);
        printf("  [%s] [%s]\n", f1, f2); fflush(stdout);
        AdsSkip(hCur, 1);
        AdsAtEOF(hCur, &bEOF);
        rows++;
    }
    if (!rows) printf("  (no rows)\n");
    AdsCloseTable(hCur);
    printf("  done.\n"); fflush(stdout);
}

int main(int argc, char** argv) {
    const char* add  = argc>1?argv[1]:"f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    const char* pw   = argc>2?argv[2]:"pmsys";

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hConn);
    if (rc) rc = AdsConnect60((UNSIGNED8*)add, ADS_REMOTE_SERVER,
                               (UNSIGNED8*)"adssys", (UNSIGNED8*)pw, 0, &hConn);
    if (rc) { printf("connect failed rc=%u\n", rc); return 1; }
    printf("connected hConn=%llu\n\n", (unsigned long long)hConn); fflush(stdout);

    q(hConn, "SELECT NAME, USER_TYPE FROM system.users ORDER BY NAME");
    q(hConn, "SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers ORDER BY USER_NAME, GROUP_NAME");

    AdsDisconnect(hConn);
    printf("disconnected.\n");
    return 0;
}
