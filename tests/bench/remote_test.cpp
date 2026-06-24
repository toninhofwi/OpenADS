// Minimal remote connection test
#include "openads/ace.h"
#include <cstdio>

int main() {
    setbuf(stderr, NULL);

    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60(
        (UNSIGNED8*)"tcp://192.168.18.184:6262//Users/anto/OpenADS/bench_data",
        ADS_REMOTE_SERVER, NULL, NULL, 0, &hConn);
    fprintf(stderr, "rc=%u handle=%llu\n", rc, (unsigned long long)hConn);

    if (hConn) {
        fprintf(stderr, "Connected!\n");
        AdsDisconnect(hConn);
    }
    return 0;
}
