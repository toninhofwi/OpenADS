#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_ODBC)

namespace {

// Live ODBC fixture: an ODBC connection string pointing at a data
// source that already holds a `clientes` table seeded with
//   (1,'Ana',10.5), (2,'Bob',NULL), (3,'Cid',0.0)
// id INTEGER PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE.
// The run script (tools/scripts/run_odbc_tests.*) creates and seeds an
// Access .accdb and exports this variable. When unset the live test is
// skipped (the backend itself is still exercised by the unit tests).
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

ADSHANDLE connect_odbc(const char* connstr) {
    const std::string uri = std::string("odbc://") + connstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

} // namespace

TEST_CASE("ABI: odbc read-only AdsOpenTable navigation") {
    const char* connstr = test_odbc_connstr();
    if (connstr == nullptr) {
        MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping live ODBC test");
        return;
    }

    ADSHANDLE hConn = connect_odbc(connstr);

    UNSIGNED8 tbl_name[32] = "clientes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    UNSIGNED16 nfields = 0;
    REQUIRE(AdsGetNumFields(hTable, &nfields) == 0);
    CHECK(nfields == 3);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    CHECK(count == 3);

    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED16 bof = 1;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    CHECK(bof == 0);

    // nome is VARCHAR(64) -> ADS_STRING, padded to the declared width.
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Ana"));

    REQUIRE(AdsSkip(hTable, 1) == 0);          // Bob
    CHECK(field_str(hTable, "saldo").empty()); // saldo is NULL

    REQUIRE(AdsSkip(hTable, 1) == 0);          // Cid
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Cid"));

    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);

    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 0);

    REQUIRE(AdsSkip(hTable, 1) == 0);          // past the end
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 1);

    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Cid"));

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#else

TEST_CASE("ABI: odbc backend disabled at compile time") {
    UNSIGNED8 uri[] = "odbc://DSN=none";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

#endif
