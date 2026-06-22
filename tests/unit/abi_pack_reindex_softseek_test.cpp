#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// Regression coverage for two engine defects surfaced by the cookbook
// authoring (both proven against the native DBFCDX baseline with the same
// .exe; see _localtest/residual_probe.prg in the cookbook worktree):
//
//   1. PACK must rebuild the controlled indexes. Before the fix, pack()
//      compacted the .dbf but left bound indexes stale, so a post-PACK
//      index walk stepped onto a dropped recno and raised 5000
//      ("record number out of range").
//
//   2. A SOFT seek with no exact match must leave the cursor positioned
//      on the next-greater key with Found()=.F. and Eof()=.F. — NOT at
//      Eof. Only a key greater than every entry yields Eof. (A prior
//      revision forced Eof on the strictly-greater landing.)

namespace fs = std::filesystem;

TEST_CASE("PACK rebuilds the controlled index (post-PACK index walk is clean)") {
    auto dir = fs::temp_directory_path() / "openads_pack_reindex";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 4; ++i) {                 // ids 1..4
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, static_cast<double>(i));
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    auto idx_path = (dir / "data.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64] = "BYID";
    UNSIGNED8 expr[64] = "ID";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0, &hIndex) == 0);

    // Delete id=2 (recno 2), then PACK -> 3 survivors renumbered 1..3.
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsPackTable(hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 3u);

    // Walk the index top->EOF. With a stale index this stepped onto the
    // dropped recno 4 and AdsSkip/AdsGetDouble returned 5000. Every step
    // must succeed and we must visit exactly the 3 survivors.
    REQUIRE(AdsGotoTop(hTable) == 0);
    int visited = 0;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (eof == 0 && visited < 50) {
        double v = 0;
        REQUIRE(AdsGetDouble(hTable, fld, &v) == 0);            // RED: 5000 here
        ++visited;
        REQUIRE(AdsSkip(hTable, 1) == 0);                        // RED: 5000 here
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(visited == 3);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("SOFT seek on an absent key lands on the next-greater key (not Eof)") {
    auto dir = fs::temp_directory_path() / "openads_softseek";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NM,C,4,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);
    UNSIGNED8 fld[] = "NM";
    for (const char* k : {"AAAA", "BBBB", "CCCC"}) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        REQUIRE(AdsSetString(hTable, fld, reinterpret_cast<UNSIGNED8*>(
                    const_cast<char*>(k)), 4) == 0);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    auto idx_path = (dir / "data.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64] = "BYNM";
    UNSIGNED8 expr[64] = "NM";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0, &hIndex) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    // Sanity: an exact seek finds an existing key.
    UNSIGNED8 kexact[8] = "BBBB";
    UNSIGNED16 fe = 0;
    REQUIRE(AdsSeek(hIndex, kexact, 4, 0, 0 /*hard*/, &fe) == 0);
    CHECK(fe == 1);

    // SOFT seek "ABBB" (absent; nearest >= is "BBBB"). Must land on BBBB,
    // Found()=.F., Eof()=.F. — before the fix this was forced to Eof.
    UNSIGNED8 ksoft[8] = "ABBB";
    UNSIGNED16 found = 9;
    REQUIRE(AdsSeek(hIndex, ksoft, 4, 0, 1 /*soft*/, &found) == 0);
    CHECK(found == 0);                              // no exact match

    UNSIGNED16 eof = 1;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 0);                               // RED: forced Eof before fix

    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), 4) == "BBBB");

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
