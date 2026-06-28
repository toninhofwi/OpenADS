// SQL URI backend smoke: sqlite:// connect, DDL, DML, filter, relations, ALTER.
// Runs in CI on every platform where OPENADS_WITH_SQLITE=ON (default).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(OPENADS_WITH_SQLITE)

namespace {

int visible_count(ADSHANDLE h) {
    if (AdsGotoTop(h) != 0) return -1;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(h, &eof) != 0) return -1;
        if (eof) break;
        ++n;
        if (AdsSkip(h, 1) != 0) return -1;
    }
    return n;
}

UNSIGNED32 record_count(ADSHANDLE h) {
    UNSIGNED32 c = 0;
    if (AdsGetRecordCount(h, 0, &c) != 0) return 0;
    return c;
}

}  // namespace

TEST_CASE("SQL URI smoke: sqlite:// DDL + DML + filter + scoped relation + ALTER") {
    const auto dir = fs::temp_directory_path() / "openads_sql_uri_smoke";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "smoke.db";

    const std::string uri = "sqlite://" + db_path.string();
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    // CREATE parent + child via AdsCreateTable (SQL DDL path).
    UNSIGNED8 pdef[] = "GRP,Character,1";
    UNSIGNED8 pname[] = "grp";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef,
                           &hP) == 0);
    for (const char* g : {"A", "B"}) {
        REQUIRE(AdsAppendRecord(hP) == 0);
        UNSIGNED8 f[8] = "GRP";
        REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)g, 1) == 0);
        REQUIRE(AdsWriteRecord(hP) == 0);
    }

    UNSIGNED8 cdef[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 cname[] = "item";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef,
                           &hC) == 0);
    struct Row { const char* grp; const char* data; } rows[] = {
        {"A", "a1"}, {"B", "b1"}, {"A", "a2"}, {"A", "a3"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == 0);
        UNSIGNED8 fg[8] = "GRP";
        UNSIGNED8 fd[8] = "DATA";
        REQUIRE(AdsSetString(hC, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == 0);
        REQUIRE(AdsWriteRecord(hC) == 0);
    }
    CHECK(record_count(hC) == 4u);

    // Index + controlling order on child (required for scoped relation).
    UNSIGNED8 idx_tag[16] = "grp";
    ADSHANDLE hIdx = 0;
    UNSIGNED16 nidx = 1;
    REQUIRE(AdsOpenIndex(hC, idx_tag, &hIdx, &nidx) == 0);
    REQUIRE(AdsSetIndexOrder(hC, idx_tag) == 0);

    // Plain relation: parent A -> child seeks first A row.
    UNSIGNED8 rexpr[] = "GRP";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == 0);
    REQUIRE(AdsGotoTop(hP) == 0);
    UNSIGNED16 found = 0;
    REQUIRE(AdsIsFound(hC, &found) == 0);
    CHECK(found == 1);

    // Scoped relation: only rows matching parent GRP are visible.
    REQUIRE(AdsClearRelation(hP) == 0);
    REQUIRE(AdsSetScopedRelation(hP, hC, rexpr) == 0);
    REQUIRE(AdsGotoTop(hP) == 0);
    CHECK(visible_count(hC) == 3);
    REQUIRE(AdsSkip(hP, 1) == 0);
    CHECK(visible_count(hC) == 1);
    REQUIRE(AdsClearRelation(hP) == 0);
    CHECK(visible_count(hC) == 4);

    // SET FILTER push-down on child.
    UNSIGNED8 filt[] = "GRP = 'A'";
    REQUIRE(AdsSetFilter(hC, filt) == 0);
    CHECK(record_count(hC) == 3u);
    REQUIRE(AdsClearAOF(hC) == 0);
    CHECK(record_count(hC) == 4u);

    // ALTER ADD via AdsRestructureTable.
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hC) == 0);
    UNSIGNED8 add[] = "NOTE,Character,4";
    REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, add,
                                nullptr, nullptr) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hC) == 0);
    CHECK(record_count(hC) == 4u);

    UNSIGNED16 nfields = 0;
    REQUIRE(AdsGetNumFields(hC, &nfields) == 0);
    CHECK(nfields == 3u);

    // ALTER DROP via AdsRestructureTable.
    REQUIRE(AdsCloseTable(hC) == 0);
    UNSIGNED8 drop[] = "NOTE";
    REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, nullptr,
                                drop, nullptr) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hC) == 0);
    REQUIRE(AdsGetNumFields(hC, &nfields) == 0);
    CHECK(nfields == 2u);

    // Navigational write smoke (dbAppend path).
    REQUIRE(AdsCloseTable(hC) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0, 0,
                         &hC) == 0);
    REQUIRE(AdsAppendRecord(hC) == 0);
    UNSIGNED8 fg[8] = "GRP";
    UNSIGNED8 fd[8] = "DATA";
    REQUIRE(AdsSetString(hC, fg, (UNSIGNED8*)"A", 1) == 0);
    REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)"a4", 2) == 0);
    REQUIRE(AdsWriteRecord(hC) == 0);
    CHECK(record_count(hC) == 5u);

    REQUIRE(AdsCloseTable(hC) == 0);
    REQUIRE(AdsCloseTable(hP) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#else

TEST_CASE("SQL URI smoke: disabled without OPENADS_WITH_SQLITE") {
    MESSAGE("OPENADS_WITH_SQLITE=OFF at compile time");
}

#endif