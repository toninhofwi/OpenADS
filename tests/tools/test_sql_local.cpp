#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>
#include <cstring>

static void sql_query(ADSHANDLE hConn, const char* sql) {
    printf("SQL: %s\n", sql); fflush(stdout);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rc = AdsExecuteSQLDirect(hConn, (UNSIGNED8*)sql, &hCur);
    printf("  rc=%u\n", rc); fflush(stdout);
    if (rc != 0 || !hCur) return;

    AdsGotoTop(hCur);

    UNSIGNED16 bEOF = ADS_FALSE;
    AdsAtEOF(hCur, &bEOF);
    int rows = 0;
    while (!bEOF && rows < 100) {
        char grp[256]={}, usr[256]={};
        UNSIGNED32 glen=sizeof(grp), ulen=sizeof(usr);
        AdsGetField(hCur, (UNSIGNED8*)"GROUP_NAME", (UNSIGNED8*)grp, &glen, ADS_NONE);
        AdsGetField(hCur, (UNSIGNED8*)"USER_NAME",  (UNSIGNED8*)usr, &ulen, ADS_NONE);
        // Trim trailing spaces
        for (int i=(int)glen-1; i>=0&&grp[i]==' ';i--) grp[i]='\0';
        for (int i=(int)ulen-1; i>=0&&usr[i]==' ';i--) usr[i]='\0';
        printf("  user=%-20s group=%s\n", usr, grp); fflush(stdout);
        AdsSkip(hCur, 1);
        AdsAtEOF(hCur, &bEOF);
        rows++;
    }
    if (!rows) printf("  (no rows)\n");
    AdsCloseTable(hCur);
}

int main(int argc, char** argv) {
    const char* add_path  = argc>1?argv[1]:"f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    const char* adssys_pw = argc>2?argv[2]:"pmsys";

    printf("Connecting LOCAL to: %s\n", add_path); fflush(stdout);

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    if (rc != 0) {
        printf("LOCAL rc=%u, trying REMOTE...\n", rc); fflush(stdout);
        rc = AdsConnect60((UNSIGNED8*)add_path, ADS_REMOTE_SERVER,
                          (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    }
    if (rc != 0) { printf("Connect failed rc=%u\n", rc); return 1; }
    printf("Connected hConn=%llu\n\n", (unsigned long long)hConn); fflush(stdout);

    sql_query(hConn,
        "SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers "
        "WHERE USER_NAME IN ('user-admin','user-backup','user-debug','user-public','root') "
        "ORDER BY USER_NAME, GROUP_NAME");

    AdsDisconnect(hConn);
    return 0;
}
