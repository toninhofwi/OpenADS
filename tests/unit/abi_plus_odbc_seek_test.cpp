#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_ODBC)

namespace {

const char* test_odbc_connstr() {
    const char* v = std::getenv("OPENADS_TEST_ODBC_CONNSTR");
    return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[64];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8  buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

UNSIGNED16 seek(ADSHANDLE hIndex, const char* key, UNSIGNED16 seek_type) {
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex,
                    reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
                    static_cast<UNSIGNED16>(std::strlen(key)),
                    /*keyType=*/0, seek_type, &found) == 0);
    return found;
}

} // namespace

TEST_CASE("ABI: odbc seek by primary-key index") {
    const char* connstr = test_odbc_connstr();
    if (connstr == nullptr) {
        MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping live ODBC test");
        return;
    }

    const std::string uri = std::string("odbc://") + connstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl_name[32] = "clientes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    UNSIGNED8 file_name[16] = "idx";
    UNSIGNED8 idx_name[16]  = "id";
    UNSIGNED8 idx_expr[16]  = "id";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex61(hTable, file_name, idx_name, idx_expr,
                             nullptr, nullptr, 0, 0, &hIndex) == 0);

    // Hard hit: id = 2 -> Bob, recno 2.
    CHECK(seek(hIndex, "2", /*hard*/ 0) == 1);
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Bob"));
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);
    UNSIGNED16 found = 0;
    REQUIRE(AdsIsFound(hTable, &found) == 0);
    CHECK(found == 1);

    // Hard miss: id = 99 -> not found.
    CHECK(seek(hIndex, "99", /*hard*/ 0) == 0);
    REQUIRE(AdsIsFound(hTable, &found) == 0);
    CHECK(found == 0);

    // Soft seek: first id >= 2 is Bob.
    CHECK(seek(hIndex, "2", ADS_SOFTSEEK) == 1);
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Bob"));

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#endif
