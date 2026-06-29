#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("BBBB");
    rec("AAAA");
    rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.4 AdsSetIndexDirection reverses traversal") {
    auto dir = fs::temp_directory_path() / "openads_m10_4_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[32]   = "tag.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    // Default ascending: GotoTop = AAAA (rec 2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Flip direction: GotoTop now = CCCC (rec 3, last in ascending).
    REQUIRE(AdsSetIndexDirection(hIdx, /*usDir=*/1) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 3);

    // Skip(+1) when descending walks prev() — moves to BBBB (rec 1).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 1);

    // Skip(+1) again → AAAA (rec 2, smallest).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Restore ascending — GotoTop = AAAA again.
    REQUIRE(AdsSetIndexDirection(hIdx, 0) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Reproduces the FWH xbrowse header-double-click bug (rddads OrdDescend
// toggle). xbrowse does `OrdDescend( , , ! OrdDescend() )`, which rddads
// maps to: read AdsIsIndexDescending; if the requested state differs from
// the current one, call AdsSetIndexDirection(hIndex, TRUE). The TRUE is a
// constant — TRUE means "flip". So the reader and writer MUST round-trip:
// each double-click has to alternate asc <-> desc. Before the fix the
// reader returned the physical flag (never changed) and the writer did an
// absolute set, so the order stuck descending after the first click.
TEST_CASE("rddads OrdDescend toggle round-trips (FWH xbrowse re-sort)") {
    auto dir = fs::temp_directory_path() / "openads_orddescend_toggle";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagt.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    // One FWH "double-click": exactly rddads' OrdDescend( , , !OrdDescend() ).
    auto double_click = [&]() {
        UNSIGNED16 cur = 0;
        REQUIRE(AdsIsIndexDescending(hIdx, &cur) == 0);
        bool want = (cur == 0);                  // !OrdDescend()
        bool fire = want ? (cur == 0) : (cur != 0);
        if (fire) REQUIRE(AdsSetIndexDirection(hIdx, 1) == 0);
    };
    auto gotop_rec = [&]() {
        REQUIRE(AdsGotoTop(hTable) == 0);
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
        return r;
    };

    // Ascending to start: GotoTop = AAAA (rec 2).
    CHECK(gotop_rec() == 2);

    // Click 1 -> descending (CCCC, rec 3). And the reader now agrees.
    double_click();
    UNSIGNED16 d = 0;
    REQUIRE(AdsIsIndexDescending(hIdx, &d) == 0);
    CHECK(d == 1);
    CHECK(gotop_rec() == 3);

    // Click 2 -> back to ascending (AAAA, rec 2). This is the click that
    // used to do nothing.
    double_click();
    REQUIRE(AdsIsIndexDescending(hIdx, &d) == 0);
    CHECK(d == 0);
    CHECK(gotop_rec() == 2);

    // Click 3 -> descending again. Confirms it keeps alternating.
    double_click();
    CHECK(gotop_rec() == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// FWH xbrowse incremental search (RddIncrSeek) on a char index: the user
// types a prefix and the browse soft-seeks to the first matching key. At
// the ACE layer that is a soft AdsSeek. This guards that an ascending
// order resolves the typed prefix to the right record — the second half of
// the fivedbu report ("incremental search doesn't work"), which in
// practice fell out of the order being wrongly stuck descending (xbrowse
// itself notes "seek is not working in the descending order").
TEST_CASE("char incremental soft-seek positions on first match (xbrowse)") {
    auto dir = fs::temp_directory_path() / "openads_incr_seek";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagi.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    // OrdSetFocus() — RddIncrSeek aborts if this is empty.
    UNSIGNED8  nm[64] = {0};
    UNSIGNED16 nmlen  = sizeof(nm);
    REQUIRE(AdsGetIndexName(hIdx, nm, &nmlen) == 0);
    CHECK(nmlen > 0);

    auto soft_seek = [&](const char* key) {
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hIdx,
                        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
                        static_cast<UNSIGNED16>(std::strlen(key)),
                        0 /*ADS_STRINGKEY*/, 1 /*soft*/, &found) == 0);
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
        return r;
    };

    CHECK(soft_seek("A") == 2);   // AAAA
    CHECK(soft_seek("B") == 1);   // BBBB
    CHECK(soft_seek("C") == 3);   // CCCC

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateIndex61 ADS_COMPOUND option builds an ascending order") {
    // rddads / X#'s ADSRDD pass ADS_COMPOUND (0x02) for every CDX/NTX
    // tag. ADS_COMPOUND must NOT be decoded as ADS_DESCENDING (0x08) —
    // doing so builds every order descending: GotoTop lands on the
    // last key and SKIP walks backward.
    auto dir = fs::temp_directory_path() / "openads_idx_compound_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1,
                         &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagc.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr, nullptr, nullptr,
                             ADS_COMPOUND, 1024, &hIdx) == 0);

    // ADS_COMPOUND alone => ascending: GotoTop = AAAA (rec 2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 2);

    // Skip(+1) walks forward (ascending) => BBBB (rec 1).
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateIndex61 ADS_DESCENDING option builds a descending order") {
    auto dir = fs::temp_directory_path() / "openads_idx_desc_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1,
                         &hTable) == 0);

    UNSIGNED8 fn[32]   = "tagd.ntx";
    UNSIGNED8 tagn[16] = "T";
    UNSIGNED8 expr[16] = "TAG";
    ADSHANDLE hIdx = 0;
    // ADS_DESCENDING | ADS_COMPOUND — the real descending request.
    REQUIRE(AdsCreateIndex61(hTable, fn, tagn, expr, nullptr, nullptr,
                             ADS_DESCENDING | ADS_COMPOUND, 1024,
                             &hIdx) == 0);

    // Descending: GotoTop = CCCC (rec 3, largest key).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
