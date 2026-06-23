#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_FIREBIRD)

namespace {

// Live embedded-Firebird fixture: OPENADS_TEST_FIREBIRD_DB points at a
// `.fdb` already seeded with a `clientes` table —
//   (1,'Ana',10.5), (2,'Bob',NULL), (3,'Cid',0.0)
// ID INTEGER PRIMARY KEY, NOME VARCHAR(64), SALDO DOUBLE PRECISION.
// The run script (tools/scripts/run_firebird_native_tests.ps1) builds the
// fixture embedded (no server / no auth) and exports the variable. When unset
// the live test is skipped — no isc_* call is made, so the delay-loaded client
// DLL is never required. Unlike firebird_connection_test (which drives the
// FirebirdConnection directly), this case exercises the whole ACE ABI surface:
// AdsConnect60 → AdsOpenTable → navigation → AdsCloseTable → AdsDisconnect.
const char* test_firebird_db() {
    const char* v = std::getenv("OPENADS_TEST_FIREBIRD_DB");
    return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

// Full server URI (firebird://USER:PASS@host:port/db), built by the server test
// harness (run_firebird_server_tests.ps1) after it launches the portable engine
// over TCP. When unset the server case is skipped.
const char* test_firebird_server_uri() {
    const char* v = std::getenv("OPENADS_TEST_FIREBIRD_SERVER");
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

ADSHANDLE connect_uri(const std::string& uri) {
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

// Embedded triple-slash form: empty authority => embedded, the remainder is
// the bare database path. The fixture path keeps its native (back)slashes —
// the parser only treats '/' / '@' as delimiters, so a Windows path with
// backslashes lands in dbpath verbatim (same string the connection test uses).
ADSHANDLE connect_firebird(const char* db) {
    return connect_uri(std::string("firebird:///") + db);
}

// Open `clientes` on an already-connected handle and walk the three seeded
// rows — the read-nav assertions are identical for the embedded and the TCP
// server backends, so both cases share this body.
void check_clientes_nav(ADSHANDLE hConn) {
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
}

} // namespace

TEST_CASE("ABI: firebird read-only AdsOpenTable navigation") {
    const char* db = test_firebird_db();
    if (db == nullptr) {
        MESSAGE("OPENADS_TEST_FIREBIRD_DB not set; skipping live Firebird test");
        return;
    }

    ADSHANDLE hConn = connect_firebird(db);
    check_clientes_nav(hConn);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

TEST_CASE("ABI: firebird server (TCP) AdsOpenTable navigation") {
    const char* uri = test_firebird_server_uri();
    if (uri == nullptr) {
        MESSAGE("OPENADS_TEST_FIREBIRD_SERVER not set; skipping live Firebird server test");
        return;
    }

    // Same Ads* surface as the embedded case, but the URI carries credentials
    // and a host:port authority — AdsConnect60 routes it to the server attach
    // form (host/port:dbpath via isc_attach_database over the wire).
    ADSHANDLE hConn = connect_uri(uri);
    check_clientes_nav(hConn);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#else

TEST_CASE("ABI: firebird backend disabled at compile time") {
    UNSIGNED8 uri[] = "firebird:///none.fdb";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

#endif
