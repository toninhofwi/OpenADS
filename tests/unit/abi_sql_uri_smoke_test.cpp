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
    REQUIRE(AdsClearFilter(hC) == 0);
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

    // Prepared statement (AdsPrepareSQL + AdsExecuteSQL) on SQL URI.
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0, 0,
                             &hC) == 0);
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
        UNSIGNED8 ins[] =
            "INSERT INTO item (GRP, DATA) VALUES (:g, :d)";
        REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
        UNSIGNED8 pg[] = "g";
        UNSIGNED8 pd[] = "d";
        REQUIRE(AdsSetString(hStmt, pg, (UNSIGNED8*)"Z", 1) == 0);
        REQUIRE(AdsSetString(hStmt, pd, (UNSIGNED8*)"zprep", 5) == 0);
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);
        CHECK(record_count(hC) == 6u);
        AdsCloseSQLStatement(hStmt);
    }

    // system.tables via AdsOpenTable (SR_MGMNT-style catalog workarea).
    {
        UNSIGNED8 sysname[] = "system.tables";
        ADSHANDLE hSys = 0;
        REQUIRE(AdsOpenTable(hConn, sysname, sysname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hSys) == 0);
        UNSIGNED32 n = 0;
        REQUIRE(AdsGetRecordCount(hSys, 0, &n) == 0);
        CHECK(n >= 2u);
        REQUIRE(AdsCloseTable(hSys) == 0);
    }

    // system.tables / system.columns catalog (SQL URI).
    {
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
        UNSIGNED8 stbl[] = "SELECT * FROM system.tables";
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, stbl, &hCur) == 0);
        REQUIRE(hCur != 0);
        std::set<std::string> names;
        REQUIRE(AdsGotoTop(hCur) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(hCur, &eof) == 0);
            if (eof) break;
            UNSIGNED8 fname[8] = "Name";
            UNSIGNED8 vbuf[256] = {};
            UNSIGNED32 vlen = sizeof(vbuf) - 1;
            REQUIRE(AdsGetField(hCur, fname, vbuf, &vlen, 0) == 0);
            std::string n(reinterpret_cast<const char*>(vbuf), vlen);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            names.insert(n);
            REQUIRE(AdsSkip(hCur, 1) == 0);
        }
        CHECK(names.count("grp") == 1);
        CHECK(names.count("item") == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 scol[] = "SELECT * FROM system.columns";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, scol, &hCur) == 0);
        REQUIRE(hCur != 0);
        int item_cols = 0;
        REQUIRE(AdsGotoTop(hCur) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(hCur, &eof) == 0);
            if (eof) break;
            UNSIGNED8 tfn[16] = "TABLE_NAME";
            UNSIGNED8 cfn[16] = "COL_NAME";
            UNSIGNED8 tbuf[64] = {};
            UNSIGNED8 cbuf[64] = {};
            UNSIGNED32 tlen = sizeof(tbuf) - 1;
            UNSIGNED32 clen = sizeof(cbuf) - 1;
            REQUIRE(AdsGetField(hCur, tfn, tbuf, &tlen, 0) == 0);
            REQUIRE(AdsGetField(hCur, cfn, cbuf, &clen, 0) == 0);
            std::string tbl(reinterpret_cast<const char*>(tbuf), tlen);
            while (!tbl.empty() && tbl.back() == ' ') tbl.pop_back();
            if (tbl == "item") ++item_cols;
            REQUIRE(AdsSkip(hCur, 1) == 0);
        }
        CHECK(item_cols == 2);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 spk[] = "SELECT * FROM system.primarykeys";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, spk, &hCur) == 0);
        REQUIRE(hCur != 0);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 sixname[] = "system.indexes";
        ADSHANDLE hIdxSys = 0;
        REQUIRE(AdsOpenTable(hConn, sixname, sixname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hIdxSys) == 0);
        REQUIRE(AdsCloseTable(hIdxSys) == 0);

        AdsCloseSQLStatement(hStmt);
    }

    REQUIRE(AdsCloseTable(hC) == 0);
    REQUIRE(AdsCloseTable(hP) == 0);

    // AdsDropTable on SQL connection.
    REQUIRE(AdsDropTable(hConn, cname, 0) == 0);
    REQUIRE(AdsDropTable(hConn, pname, 0) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#else

TEST_CASE("SQL URI smoke: disabled without OPENADS_WITH_SQLITE") {
    MESSAGE("OPENADS_WITH_SQLITE=OFF at compile time");
}

#endif