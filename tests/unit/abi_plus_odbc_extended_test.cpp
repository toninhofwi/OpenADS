#include "doctest.h"
#include "openads/ace.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_ODBC)
namespace {
const char* odbc_connstr() {
    const char* v = std::getenv("OPENADS_TEST_ODBC_CONNSTR");
    return (v && v[0]) ? v : nullptr;
}
ADSHANDLE connect_odbc(const char* connstr) {
    const std::string uri = std::string("odbc://") + connstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}
std::string read_str(ADSHANDLE h, const char* f) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 buf[256] = {0}; UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(h, fb, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}
} // namespace

TEST_CASE("ABI: odbc composite PK + date + decimal navigation") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }
    ADSHANDLE hConn = connect_odbc(cs);
    UNSIGNED8 t[32] = "pedidos";
    ADSHANDLE h = 0;
    UNSIGNED32 rc = AdsOpenTable(hConn, t, t, ADS_DEFAULT, 0, 0, 0, ADS_READONLY, &h);
    if (rc != 0) {
        MESSAGE("pedidos fixture absent; skipping (table open failed)");
        AdsDisconnect(hConn);
        return;
    }

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(h, 0, &count) == 0);
    CHECK(count == 3);                         // composite-PK snapshot has 3 rows

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(h, &nf) == 0);
    CHECK(nf == 4);

    REQUIRE(AdsGotoTop(h) == 0);               // ordered by (cliente_id,item_id)
    CHECK(read_str(h, "qtd").find("2.5") != std::string::npos);   // decimal precision
    CHECK(read_str(h, "data").find("2026") != std::string::npos); // date round-trips

    REQUIRE(AdsCloseTable(h) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}
#endif
