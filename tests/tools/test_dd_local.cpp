#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ace.h"
#include <cstdio>

int main(int argc, char** argv) {
    const char* add_path = argc>1?argv[1]:"f:\\OpenADS\\testdata\\pmsys\\pmsys.add";
    const char* adssys_pw = argc>2?argv[2]:"pmsys";

    printf("Connecting LOCAL to: %s\n", add_path); fflush(stdout);

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60((UNSIGNED8*)add_path, ADS_LOCAL_SERVER,
                                  (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    if (rc != 0) {
        printf("LOCAL failed rc=%u, trying REMOTE...\n", rc); fflush(stdout);
        rc = AdsConnect60((UNSIGNED8*)add_path, ADS_REMOTE_SERVER,
                          (UNSIGNED8*)"adssys", (UNSIGNED8*)adssys_pw, 0, &hConn);
    }
    if (rc != 0) { printf("Connect failed rc=%u\n", rc); return 1; }
    printf("Connected hConn=%llu\n\n", (unsigned long long)hConn); fflush(stdout);

    const char* users[] = {
        "user-admin","user-backup","user-debug","user-public","root","RCB", nullptr
    };
    for (int i = 0; users[i]; i++) {
        unsigned char buf[4096] = {}; UNSIGNED16 len = (UNSIGNED16)sizeof(buf);
        rc = AdsDDGetUserProperty(hConn, (UNSIGNED8*)users[i], ADS_DD_USER_GROUP_MEMBERSHIP,
                                  buf, &len);
        if (rc == 0)
            printf("  %-20s groups=[%.*s]\n", users[i], (int)len, (char*)buf);
        else
            printf("  %-20s rc=%u\n", users[i], rc);
        fflush(stdout);
    }

    AdsDisconnect(hConn);
    return 0;
}
