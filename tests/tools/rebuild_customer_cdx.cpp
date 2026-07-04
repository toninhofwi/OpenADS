// Rebuild testdata/invoices/customer.cdx with correct CUSTNO/CUSTNAME keys.
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int fail(const char* msg, UNSIGNED32 rc = 0) {
    std::fprintf(stderr, "FAIL: %s", msg);
    if (rc) std::fprintf(stderr, " (rc=%u)", rc);
    std::fputc('\n', stderr);
    return 1;
}

int main(int argc, char** argv) {
    const char* data = (argc > 1) ? argv[1] : "C:/OpenADS/testdata/invoices";
    char cdx[512];
    std::snprintf(cdx, sizeof(cdx), "%s/customer.cdx", data);
    std::remove(cdx);

    std::string sp = data;
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0, hT = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) != 0)
        return fail("AdsConnect60");
    UNSIGNED8 tname[] = "customer.dbf";
    if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hT) != 0)
        return fail("AdsOpenTable");

    struct Tag { const char* name; const char* expr; };
    const Tag tags[] = {
        {"CUSTNO",   "CUSTNO"},
        {"CUSTNAME", "NAME"},
    };
    for (const auto& tg : tags) {
        ADSHANDLE hIx = 0;
        std::vector<UNSIGNED8> bag{'c','u','s','t','o','m','e','r','.','c','d','x',0};
        std::vector<UNSIGNED8> t(tg.name, tg.name + std::strlen(tg.name));
        t.push_back(0);
        std::vector<UNSIGNED8> e(tg.expr, tg.expr + std::strlen(tg.expr));
        e.push_back(0);
        UNSIGNED32 rc = AdsCreateIndex61(hT, bag.data(), t.data(), e.data(),
                                         nullptr, nullptr, ADS_COMPOUND, 512, &hIx);
        if (rc != 0) return fail("AdsCreateIndex61", rc);
        if (AdsCloseIndex(hIx) != 0) return fail("AdsCloseIndex");
    }

    if (AdsReindex(hT) != 0) return fail("AdsReindex");
    if (AdsCloseTable(hT) != 0) return fail("AdsCloseTable");
    if (AdsDisconnect(hConn) != 0) return fail("AdsDisconnect");

    std::printf("OK: rebuilt %s\n", cdx);
    return 0;
}