#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

typedef unsigned int       UNSIGNED32;
typedef unsigned long long ADSHANDLE;
typedef unsigned char      UNSIGNED8;
typedef unsigned short     UNSIGNED16;

#define ADS_LOCAL_SERVER  1

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
    printf("SQL: %s\n  rc=%u hc=%llu\n", sql, rc, (unsigned long long)hc);
    if (rc==0 && hc) {
        UNSIGNED16 bEOF=0;
        int rows=0;
        while (pfnEOF(hc,&bEOF)==0 && !bEOF && rows<50) {
            char buf1[256]={}, buf2[256]={};
            UNSIGNED32 len1=255, len2=255;
            pfnField(hc,(UNSIGNED8*)col1,(UNSIGNED8*)buf1,&len1,0);
            if (col2) pfnField(hc,(UNSIGNED8*)col2,(UNSIGNED8*)buf2,&len2,0);
            if (col2) printf("  [%s] [%s]\n", buf1, buf2);
            else      printf("  [%s]\n", buf1);
            pfnNext(hc);
            rows++;
        }
        printf("  total rows: %d\n", rows);
        pfnClose(hc);
    }
}

int main() {
    HMODULE hDll = LoadLibraryA("ace64.dll");
    if (!hDll) { printf("LoadLibrary ace64.dll failed %u\n", GetLastError()); return 1; }

    pfnConn  = (PFN_Connect)       GetProcAddress(hDll,(LPCSTR)315);
    pfnSQL   = (PFN_ExecSQL)       GetProcAddress(hDll,(LPCSTR)265);
    pfnDisc  = (PFN_Disc)          GetProcAddress(hDll,(LPCSTR)32);
    pfnClose = (PFN_Close)         GetProcAddress(hDll,(LPCSTR)19);
    pfnEOF   = (PFN_EOF)           GetProcAddress(hDll,"AdsAtEOF");
    pfnNext  = (PFN_Next)          GetProcAddress(hDll,"AdsSkip");
    pfnField = (PFN_GetField)       GetProcAddress(hDll,"AdsGetField");
    pfnUserProp=(PFN_DDGetUserProp) GetProcAddress(hDll,"AdsDDGetUserProperty");

    printf("ordinals OK. AdsAtEOF=%p AdsSkip=%p AdsGetField=%p AdsDDGetUserProp=%p\n",
        pfnEOF,pfnNext,pfnField,pfnUserProp); fflush(stdout);

    ADSHANDLE h=0;
    UNSIGNED32 rc = pfnConn((UNSIGNED8*)"f:\\OpenADS\\testdata\\pmsys\\pmsys.add",
        ADS_LOCAL_SERVER,(UNSIGNED8*)"adssys",(UNSIGNED8*)"pmsys",0,&h);
    printf("connect rc=%u h=%llu\n",rc,(unsigned long long)h); fflush(stdout);
    if (rc) return 1;

    // Try system tables that ARC uses for group membership
    run_query(h, "SELECT Name FROM system.users ORDER BY Name",            "Name");
    run_query(h, "SELECT Name FROM system.usergroups ORDER BY Name",       "Name");
    run_query(h, "SELECT User_Name, Group_Name FROM system.usergroupmembers ORDER BY User_Name", "User_Name","Group_Name");
    run_query(h, "SELECT User_Name, Group_Name FROM system.usergroupmembers WHERE Group_Name LIKE 'DB:%'", "User_Name","Group_Name");

    // Try AdsDDGetUserProperty(h, user, 1102, buf, &len) for group list
    if (pfnUserProp) {
        const char* users[] = {"adssys","pmsys_admin","pmsys_user",nullptr};
        for (int i=0; users[i]; i++) {
            char buf[1024]={};
            UNSIGNED16 len=1023;
            rc = pfnUserProp(h,(UNSIGNED8*)users[i],1102,(UNSIGNED8*)buf,&len);
            printf("UserProp[1102] user=%s rc=%u len=%u val=[%s]\n",
                users[i],rc,len,rc==0?buf:"(err)");
        }
    }

    pfnDisc(h);
    printf("done\n");
    return 0;
}
