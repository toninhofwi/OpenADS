#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

typedef unsigned int       UNSIGNED32;
typedef unsigned long long ADSHANDLE;
typedef unsigned char      UNSIGNED8;
typedef unsigned short     UNSIGNED16;

#define ADS_LOCAL_SERVER  1
#define ADS_REMOTE_SERVER 2

extern "C" {
typedef UNSIGNED32 (__stdcall *PFN_Connect)(UNSIGNED8* path, UNSIGNED16 server,
    UNSIGNED8* user, UNSIGNED8* pw, UNSIGNED32 opts, ADSHANDLE* ph);
typedef UNSIGNED32 (__stdcall *PFN_ExecSQL)(ADSHANDLE h, UNSIGNED8* sql, ADSHANDLE* phc);
typedef UNSIGNED32 (__stdcall *PFN_Close)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_Disc)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_EOF)(ADSHANDLE h, UNSIGNED16* pb);
typedef UNSIGNED32 (__stdcall *PFN_Next)(ADSHANDLE h);
typedef UNSIGNED32 (__stdcall *PFN_GetField)(ADSHANDLE h, UNSIGNED8* name, UNSIGNED8* buf, UNSIGNED32* len, UNSIGNED16 raw);
typedef UNSIGNED32 (__stdcall *PFN_DDGetUserProp)(ADSHANDLE h, UNSIGNED8* user, UNSIGNED16 prop, UNSIGNED8* buf, UNSIGNED16* len);
}

static PFN_Connect  pfnConn;
static PFN_ExecSQL  pfnSQL;
static PFN_Disc     pfnDisc;
static PFN_Close    pfnClose;
static PFN_EOF      pfnEOF;
static PFN_Next     pfnNext;
static PFN_GetField pfnField;
static PFN_DDGetUserProp pfnUserProp;

static void run_query(ADSHANDLE h, const char* sql, const char* col1, const char* col2=nullptr) {
    ADSHANDLE hc=0;
    UNSIGNED32 rc = pfnSQL(h, (UNSIGNED8*)sql, &hc);
    printf("SQL: %s\n  rc=%u\n", sql, rc);
    if (rc==0 && hc) {
        UNSIGNED16 bEOF=0;
        int rows=0;
        while (pfnEOF(hc,&bEOF)==0 && !bEOF && rows<100) {
            char buf1[256]={}, buf2[256]={};
            UNSIGNED32 len1=255, len2=255;
            pfnField(hc,(UNSIGNED8*)col1,(UNSIGNED8*)buf1,&len1,0);
            if (col2) pfnField(hc,(UNSIGNED8*)col2,(UNSIGNED8*)buf2,&len2,0);
            if (col2) printf("  [%s] [%s]\n", buf1, buf2);
            else      printf("  [%s]\n", buf1);
            pfnNext(hc);
            rows++;
        }
        printf("  rows=%d\n", rows);
        pfnClose(hc);
    }
    fflush(stdout);
}

static void try_connect(const char* path, const char* pw, int srvtype, const char* srvname) {
    ADSHANDLE h=0;
    UNSIGNED32 rc = pfnConn((UNSIGNED8*)path, (UNSIGNED16)srvtype,
        (UNSIGNED8*)"adssys",(UNSIGNED8*)pw,0,&h);
    printf("\n--- %s [%s] pw=%s rc=%u h=%llu ---\n", srvname, path, pw, rc, (unsigned long long)h);
    fflush(stdout);
    if (rc) return;

    run_query(h,"SELECT Name FROM system.users ORDER BY Name","Name");
    run_query(h,"SELECT User_Name,Group_Name FROM system.usergroupmembers WHERE Group_Name LIKE 'DB:%'","User_Name","Group_Name");

    if (pfnUserProp) {
        char buf[1024]={};
        UNSIGNED16 len=1023;
        rc = pfnUserProp(h,(UNSIGNED8*)"adssys",1102,(UNSIGNED8*)buf,&len);
        printf("UserProp[1102] adssys rc=%u val=[%s]\n", rc, rc==0?buf:"(err)");
        fflush(stdout);
    }
    pfnDisc(h);
}

int main() {
    HMODULE hDll = LoadLibraryA("ace64.dll");
    if (!hDll) { printf("LoadLibrary failed %u\n", GetLastError()); return 1; }
    pfnConn  = (PFN_Connect)       GetProcAddress(hDll,(LPCSTR)315);
    pfnSQL   = (PFN_ExecSQL)       GetProcAddress(hDll,(LPCSTR)265);
    pfnDisc  = (PFN_Disc)          GetProcAddress(hDll,(LPCSTR)32);
    pfnClose = (PFN_Close)         GetProcAddress(hDll,(LPCSTR)19);
    pfnEOF   = (PFN_EOF)           GetProcAddress(hDll,"AdsAtEOF");
    pfnNext  = (PFN_Next)          GetProcAddress(hDll,"AdsSkip");
    pfnField = (PFN_GetField)       GetProcAddress(hDll,"AdsGetField");
    pfnUserProp=(PFN_DDGetUserProp) GetProcAddress(hDll,"AdsDDGetUserProperty");

    // Try REMOTE with different path formats
    try_connect("f:\\OpenADS\\testdata\\pmsys\\pmsys.add","pmsys", ADS_REMOTE_SERVER, "REMOTE-local-path");
    try_connect("\\\\SOOKIE\\f$\\OpenADS\\testdata\\pmsys\\pmsys.add","pmsys", ADS_REMOTE_SERVER, "REMOTE-UNC");
    try_connect("\\\\127.0.0.1\\f$\\OpenADS\\testdata\\pmsys\\pmsys.add","pmsys", ADS_REMOTE_SERVER, "REMOTE-127UNC");
    try_connect("f:\\OpenADS\\testdata\\pmsys\\pmsys.add","pmsys", ADS_LOCAL_SERVER, "LOCAL");

    printf("done\n");
    return 0;
}
