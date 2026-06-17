#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>

typedef unsigned int       UNSIGNED32;
typedef int                SIGNED32;
typedef unsigned long long ADSHANDLE;
typedef unsigned char      UNSIGNED8;
typedef unsigned short     UNSIGNED16;

#define ADS_LOCAL_SERVER  1
#define ADS_REMOTE_SERVER 2

extern "C" {
typedef UNSIGNED32 (__stdcall *PFN_Connect)(UNSIGNED8* path, UNSIGNED16 server,
    UNSIGNED8* user, UNSIGNED8* pw, UNSIGNED32 opts, ADSHANDLE* ph);
typedef UNSIGNED32 (__stdcall *PFN_CreateStmt)(ADSHANDLE hConn, ADSHANDLE* phStmt);
typedef UNSIGNED32 (__stdcall *PFN_ExecSQL)(ADSHANDLE hStmt, UNSIGNED8* sql, ADSHANDLE* phc);
typedef UNSIGNED32 (__stdcall *PFN_Close)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_Disc)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_EOF)(ADSHANDLE h, UNSIGNED16* pb);
typedef UNSIGNED32 (__stdcall *PFN_Skip)(ADSHANDLE h, SIGNED32 n);
typedef UNSIGNED32 (__stdcall *PFN_GetField)(ADSHANDLE h, UNSIGNED8* name, UNSIGNED8* buf, UNSIGNED32* len, UNSIGNED16 raw);
typedef UNSIGNED32 (__stdcall *PFN_DDGetUserProp)(ADSHANDLE h, UNSIGNED8* user, UNSIGNED16 prop, UNSIGNED8* buf, UNSIGNED16* len);
}

static PFN_Connect    pfnConn;
static PFN_CreateStmt pfnCreateStmt;
static PFN_ExecSQL    pfnSQL;
static PFN_Disc       pfnDisc;
static PFN_Close      pfnClose;
static PFN_EOF        pfnEOF;
static PFN_Skip       pfnSkip;
static PFN_GetField   pfnField;
static PFN_DDGetUserProp pfnUserProp;

static char* rtrim(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1]==' ' || s[n-1]=='\0')) n--;
    s[n] = '\0';
    return s;
}

static void run_query(ADSHANDLE hStmt, const char* sql, const char* col1, const char* col2=nullptr) {
    ADSHANDLE hc=0;
    UNSIGNED32 rc = pfnSQL(hStmt, (UNSIGNED8*)sql, &hc);
    printf("SQL: %s\n  rc=%u\n", sql, rc);
    if (rc==0 && hc) {
        UNSIGNED16 bEOF=0;
        int rows=0;
        while (pfnEOF(hc,&bEOF)==0 && !bEOF && rows<200) {
            char buf1[512]={}, buf2[512]={};
            UNSIGNED32 len1=511, len2=511;
            pfnField(hc,(UNSIGNED8*)col1,(UNSIGNED8*)buf1,&len1,0);
            if (col2) pfnField(hc,(UNSIGNED8*)col2,(UNSIGNED8*)buf2,&len2,0);
            rtrim(buf1); if (col2) rtrim(buf2);
            if (col2) printf("  [%s] [%s]\n", buf1, buf2);
            else      printf("  [%s]\n", buf1);
            pfnSkip(hc, 1);
            rows++;
        }
        printf("  total rows shown: %d (eof=%d)\n", rows, (int)bEOF);
        pfnClose(hc);
    }
    fflush(stdout);
}

static void try_server(const char* label, int srvtype) {
    ADSHANDLE h=0;
    UNSIGNED32 rc = pfnConn((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        (UNSIGNED16)srvtype,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    printf("\n=== %s rc=%u ===\n", label, rc);
    if (rc) return;

    ADSHANDLE hStmt=0;
    rc = pfnCreateStmt(h, &hStmt);
    printf("CreateSQLStatement rc=%u hStmt=%llu\n", rc, (unsigned long long)hStmt);
    if (rc) { pfnDisc(h); return; }

    run_query(hStmt,"SELECT Name FROM system.users ORDER BY Name","Name");
    run_query(hStmt,"SELECT User_Name,Group_Name FROM system.usergroupmembers ORDER BY User_Name,Group_Name","User_Name","Group_Name");
    run_query(hStmt,"SELECT User_Name,Group_Name FROM system.usergroupmembers WHERE Group_Name LIKE 'DB:%' ORDER BY User_Name","User_Name","Group_Name");

    if (pfnUserProp) {
        const char* users[] = {"adssys","RCB",nullptr};
        for (int i=0; users[i]; i++) {
            char buf[2048]={};
            UNSIGNED16 len=2047;
            rc = pfnUserProp(h,(UNSIGNED8*)users[i],1102,(UNSIGNED8*)buf,&len);
            printf("UserProp[1102] user=%s rc=%u val=[%s]\n", users[i], rc, rc==0?buf:"(err)");
        }
    }
    fflush(stdout);

    pfnClose(hStmt);
    pfnDisc(h);
}

int main() {
    HMODULE hDll = LoadLibraryA("ace64.dll");
    if (!hDll) { printf("LoadLibrary failed %u\n", GetLastError()); return 1; }
    pfnConn      = (PFN_Connect)       GetProcAddress(hDll,(LPCSTR)315);
    pfnCreateStmt= (PFN_CreateStmt)    GetProcAddress(hDll,"AdsCreateSQLStatement");
    pfnSQL       = (PFN_ExecSQL)       GetProcAddress(hDll,(LPCSTR)265);
    pfnDisc      = (PFN_Disc)          GetProcAddress(hDll,(LPCSTR)32);
    pfnClose     = (PFN_Close)         GetProcAddress(hDll,(LPCSTR)19);
    pfnEOF       = (PFN_EOF)           GetProcAddress(hDll,"AdsAtEOF");
    pfnSkip      = (PFN_Skip)          GetProcAddress(hDll,"AdsSkip");
    pfnField     = (PFN_GetField)       GetProcAddress(hDll,"AdsGetField");
    pfnUserProp  = (PFN_DDGetUserProp)  GetProcAddress(hDll,"AdsDDGetUserProperty");

    try_server("LOCAL",  ADS_LOCAL_SERVER);
    try_server("REMOTE", ADS_REMOTE_SERVER);

    printf("\ndone\n");
    return 0;
}
