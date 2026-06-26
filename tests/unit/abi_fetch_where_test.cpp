// tests/unit/abi_fetch_where_test.cpp
// Task 2 ABI verification: AdsFetchWhere* result-set exports.
//
// Test cases:
//   1. Local table → AdsFetchWhere returns non-zero (not applicable; caller
//      falls back to the classic client-side scan path).
//   2. Remote wire: filtered batch with field values and per-row recnos.
//   3. Remote wire: count-only (pszCols = nullptr → no column data).
//
// Row indices passed to the accessor functions are 0-based.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// ── fixture helpers ──────────────────────────────────────────────────────────
namespace {

fs::path fw_tmp_dir() {
    return fs::temp_directory_path() / "openads_fw_abi_test";
}

void fw_wipe() {
    std::error_code ec;
    fs::remove_all(fw_tmp_dir(), ec);
    fs::create_directories(fw_tmp_dir(), ec);
}

// Seed a local CDX/DBF table "fw.dbf" with three records:
//   recno 1 → NM = "A"
//   recno 2 → NM = "B"
//   recno 3 → NM = "C"
void seed_nm_fixture(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,4,0";
    UNSIGNED8 tname[] = "fw.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);

    UNSIGNED8 fld[] = "NM";
    auto set_nm = [&](const char* val) {
        UNSIGNED32 len = static_cast<UNSIGNED32>(std::strlen(val));
        REQUIRE(AdsSetString(hTable, fld,
                             reinterpret_cast<UNSIGNED8*>(const_cast<char*>(val)),
                             len) == AE_SUCCESS);
    };

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_nm("A"); REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_nm("B"); REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_nm("C"); REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

// Trim trailing ASCII spaces (CHARACTER fields may be padded to field width).
std::string trim_right(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

// ── 1. Local table — not applicable ─────────────────────────────────────────
TEST_CASE("AdsFetchWhere reports not-applicable on a local table") {
    fw_wipe();
    auto dir = fw_tmp_dir();

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,4,0";
    UNSIGNED8 tname[] = "local_fw.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);

    UNSIGNED8 expr[] = "NM >= 'B'";
    ADSHANDLE hRes   = 0;
    UNSIGNED32 rc = AdsFetchWhere(hTable, expr, nullptr, 100, 0, &hRes);
    CHECK(rc != 0);    // must be non-zero: not applicable on a local table
    CHECK(hRes == 0);  // must NOT set a result handle on failure

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

// ── 2. Remote wire: filtered batch + recnos + field access ──────────────────
TEST_CASE("AdsFetchWhere remote wire: filtered batch with recnos and field access") {
    fw_wipe();
    auto dir = fw_tmp_dir();
    seed_nm_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "fw.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    // Position at top before the filtered scan.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    // Fetch rows where NM >= 'B', requesting column "NM" and per-row recnos.
    UNSIGNED8 expr[] = "NM >= 'B'";
    UNSIGNED8 cols[] = "NM";
    ADSHANDLE hRes   = 0;
    UNSIGNED32 rc = AdsFetchWhere(hTable, expr, cols,
                                  /*ulMaxRows*/ 100,
                                  /*ulFlags*/  0x01u, // WANT_RECNO
                                  &hRes);
    REQUIRE(rc == AE_SUCCESS);
    CHECK(hRes != 0);

    // Matched rows: "B" (recno 2) and "C" (recno 3) → 2 rows.
    UNSIGNED32 nrows = 0;
    REQUIRE(AdsFetchWhereRows(hRes, &nrows) == AE_SUCCESS);
    CHECK(nrows == 2u);

    // Per-row record numbers (1-based DBF recnos).
    UNSIGNED32 rec0 = 0, rec1 = 0;
    REQUIRE(AdsFetchWhereRecno(hRes, 0, &rec0) == AE_SUCCESS);
    REQUIRE(AdsFetchWhereRecno(hRes, 1, &rec1) == AE_SUCCESS);
    CHECK(rec0 == 2u);  // record 2 holds "B"
    CHECK(rec1 == 3u);  // record 3 holds "C"

    // Field values for column "NM" (may be space-padded to 4 chars).
    UNSIGNED8 nm_fld[] = "NM";
    UNSIGNED8 buf[32]{};
    UNSIGNED16 blen = sizeof(buf);
    REQUIRE(AdsFetchWhereField(hRes, 0, nm_fld, buf, &blen) == AE_SUCCESS);
    CHECK(trim_right(std::string(reinterpret_cast<char*>(buf), blen)) == "B");

    blen = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsFetchWhereField(hRes, 1, nm_fld, buf, &blen) == AE_SUCCESS);
    CHECK(trim_right(std::string(reinterpret_cast<char*>(buf), blen)) == "C");

    // EOF: all three records were examined → server reached end-of-table.
    UNSIGNED16 eof_flag = 0;
    REQUIRE(AdsFetchWhereEof(hRes, &eof_flag) == AE_SUCCESS);
    CHECK(eof_flag != 0);

    // Close frees the handle; subsequent accessor calls must fail.
    REQUIRE(AdsFetchWhereClose(hRes) == AE_SUCCESS);
    UNSIGNED32 dummy = 0;
    CHECK(AdsFetchWhereRows(hRes, &dummy) != AE_SUCCESS);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

// ── 3. Remote wire: count-only (no columns, no recnos) ──────────────────────
TEST_CASE("AdsFetchWhere remote wire: count-only (no columns, flags=0)") {
    fw_wipe();
    auto dir = fw_tmp_dir();
    seed_nm_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "fw.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    // pszCols = nullptr → no column data; flags = 0 → no recnos.
    UNSIGNED8 expr[] = "NM >= 'B'";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsFetchWhere(hTable, expr, nullptr, 100, 0, &hRes) == AE_SUCCESS);

    UNSIGNED32 nrows = 0;
    REQUIRE(AdsFetchWhereRows(hRes, &nrows) == AE_SUCCESS);
    CHECK(nrows == 2u);  // "B" and "C" matched

    // EOF: entire table was scanned.
    UNSIGNED16 eof_flag = 0;
    REQUIRE(AdsFetchWhereEof(hRes, &eof_flag) == AE_SUCCESS);
    CHECK(eof_flag != 0);

    REQUIRE(AdsFetchWhereClose(hRes) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
