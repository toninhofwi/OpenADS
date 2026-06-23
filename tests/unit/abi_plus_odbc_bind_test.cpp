#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

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

ADSHANDLE open_clientes(ADSHANDLE hConn) {
    UNSIGNED8 t[32] = "clientes";
    ADSHANDLE h = 0;
    REQUIRE(AdsOpenTable(hConn, t, t, ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &h) == 0);
    return h;
}

void set_str(ADSHANDLE h, const char* f, const char* v) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 vb[256]; std::memcpy(vb, v, std::strlen(v) + 1);
    REQUIRE(AdsSetString(h, fb, vb, static_cast<UNSIGNED32>(std::strlen(v))) == 0);
}

void set_dbl(ADSHANDLE h, const char* f, double v) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    REQUIRE(AdsSetDouble(h, fb, v) == 0);
}

std::string read_str(ADSHANDLE h, const char* f) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 buf[256] = {0}; UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(h, fb, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

UNSIGNED32 count_rows(ADSHANDLE h) {
    UNSIGNED32 n = 0; REQUIRE(AdsGetRecordCount(h, 0, &n) == 0); return n;
}

} // namespace

// Prove that seeking a key containing an embedded single quote works safely
// via the bound-parameter path (not string interpolation). Before Task 3 the
// `seek_index` implementation built a SQL literal, which would have required
// escaping or would break the query; after Task 3 the key is passed via
// SQLBindParameter, making the quote invisible to the SQL parser.
TEST_CASE("ABI: odbc seek key with embedded quote binds safely") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }

    ADSHANDLE hConn  = connect_odbc(cs);
    ADSHANDLE hTable = open_clientes(hConn);

    // Append a row whose name contains a single quote.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    set_str(hTable, "id", "7");
    set_str(hTable, "nome", "O'Brien");
    set_dbl(hTable, "saldo", 5.0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    CHECK(count_rows(hTable) == 4);

    // Create an index on `nome` so that AdsSeek can route through
    // seek_index on the ODBC backend (mirrors the pattern in
    // abi_plus_odbc_seek_test.cpp which creates an index on `id`).
    UNSIGNED8 file_name[16] = "idx_nome";
    UNSIGNED8 idx_name[16]  = "nome";
    UNSIGNED8 idx_expr[16]  = "nome";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex61(hTable, file_name, idx_name, idx_expr,
                             nullptr, nullptr, 0, 0, &hIndex) == 0);

    // Seek the row by its name (which contains a single quote).
    const char*  key_str = "O'Brien";
    UNSIGNED8    key[64];
    std::memcpy(key, key_str, std::strlen(key_str) + 1);
    UNSIGNED16   found = 0;
    REQUIRE(AdsSeek(hIndex,
                    key,
                    static_cast<UNSIGNED16>(std::strlen(key_str)),
                    /*keyType=*/0,
                    /*seekType=*/0,   // ADS_HARDSEEK
                    &found) == 0);
    CHECK(found == 1);
    CHECK(read_str(hTable, "nome").find("O'Brien") != std::string::npos);

    UNSIGNED16 is_found = 0;
    REQUIRE(AdsIsFound(hTable, &is_found) == 0);
    CHECK(is_found == 1);

    REQUIRE(AdsCloseIndex(hIndex) == 0);

    // Clean up: delete the appended row and restore the table to 3 rows.
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    CHECK(count_rows(hTable) == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

TEST_CASE("ABI: odbc write binds NULL and unicode correctly") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }
    ADSHANDLE hConn = connect_odbc(cs);
    ADSHANDLE h = open_clientes(hConn);

    // Append id=8 with a unicode name and an EMPTY saldo (numeric) -> NULL.
    REQUIRE(AdsAppendRecord(h) == 0);
    set_str(h, "id", "8");
    set_str(h, "nome", "Jo\xC3\xA3o");   // "João" UTF-8
    set_str(h, "saldo", "");              // empty numeric -> SQL NULL
    REQUIRE(AdsWriteRecord(h) == 0);

    // Re-find it and verify: nome round-trips, saldo reads back NULL (empty).
    UNSIGNED8 key[8]  = "8";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(h, key, 1, ADS_SEEKEQ, &found) == 0);
    CHECK(found == 1);
    CHECK(read_str(h, "nome").find("Jo\xC3\xA3o") != std::string::npos);
    CHECK(read_str(h, "saldo").empty());   // NULL reads as empty string

    REQUIRE(AdsDeleteRecord(h) == 0);
    CHECK(count_rows(h) == 3);
    REQUIRE(AdsCloseTable(h) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#endif // OPENADS_WITH_ODBC
