// AdsSetRelation — parent→child work-area relation. Moving the parent's
// cursor re-seeks the child's controlling order to the key produced by
// evaluating the relation expression against the parent's current record.
// A miss leaves the child at EOF (Clipper / ACE dbSetRelation semantics).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string child_data(ADSHANDLE hChild) {
    UNSIGNED8 field[8] = "DATA";
    UNSIGNED8 buf[32];
    UNSIGNED32 len = sizeof(buf);
    if (AdsGetString(hChild, field, buf, &len, 0) != openads::AE_SUCCESS)
        return "<err>";
    std::string s((char*)buf, len);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
}  // namespace

TEST_CASE("AdsSetRelation drives a child by a character key as the parent moves") {
    const auto dir = fs::temp_directory_path() / "openads_setrelation_char";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // Parent: a list of keys (one CHAR field).
    UNSIGNED8 pdef[]  = "CODE,Character,4";
    UNSIGNED8 pname[] = "par";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
            == openads::AE_SUCCESS);
    const char* codes[] = {"AAAA", "BBBB", "CCCC", "ZZZZ"};
    for (const char* c : codes) {
        REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
        UNSIGNED8 f[8] = "CODE";
        REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)c, 4) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
    }

    // Child: CODE + DATA, indexed on CODE. "ZZZZ" is intentionally absent.
    UNSIGNED8 cdef[]  = "CODE,Character,4;DATA,Character,8";
    UNSIGNED8 cname[] = "chi";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
            == openads::AE_SUCCESS);
    UNSIGNED8 bag[]  = "chi.cdx";
    UNSIGNED8 tag[]  = "BYCODE";
    UNSIGNED8 cexpr[] = "CODE";
    ADSHANDLE hCI = 0;
    REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
            == openads::AE_SUCCESS);
    struct { const char* code; const char* data; } rows[] = {
        {"AAAA", "alpha"}, {"BBBB", "beta"}, {"CCCC", "gamma"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
        UNSIGNED8 fc[8] = "CODE";
        UNSIGNED8 fd[8] = "DATA";
        REQUIRE(AdsSetString(hC, fc, (UNSIGNED8*)r.code, 4) == openads::AE_SUCCESS);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
    }

    // Relate child to parent on the CODE expression.
    UNSIGNED8 rexpr[] = "CODE";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    // SetRelation positions the child against the parent's current record.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "alpha");

    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // BBBB
    CHECK(child_data(hC) == "beta");

    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CCCC
    CHECK(child_data(hC) == "gamma");

    // Parent on "ZZZZ" — no child match: the child lands at EOF.
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // ZZZZ
    UNSIGNED16 eof = 9;
    REQUIRE(AdsAtEOF(hC, &eof) == openads::AE_SUCCESS);
    CHECK(eof == 1);

    // Back up to a hit: the relation re-seeks the child.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);     // AAAA
    CHECK(child_data(hC) == "alpha");

    // After clearing, the child no longer tracks the parent.
    REQUIRE(AdsClearRelation(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // BBBB, but child stays
    CHECK(child_data(hC) == "alpha");

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetRelation drives a child by a numeric key as the parent moves") {
    const auto dir = fs::temp_directory_path() / "openads_setrelation_num";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // Parent rows hold the foreign key as a numeric field.
    UNSIGNED8 pdef[]  = "CID,Numeric,4,0";
    UNSIGNED8 pname[] = "ord";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
            == openads::AE_SUCCESS);
    const double ids[] = {30, 10, 20};
    for (double id : ids) {
        REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
        UNSIGNED8 f[8] = "CID";
        REQUIRE(AdsSetDouble(hP, f, id) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
    }

    // Child keyed on its numeric ID.
    UNSIGNED8 cdef[]  = "ID,Numeric,4,0;DATA,Character,8";
    UNSIGNED8 cname[] = "cust";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
            == openads::AE_SUCCESS);
    UNSIGNED8 bag[]  = "cust.cdx";
    UNSIGNED8 tag[]  = "BYID";
    UNSIGNED8 cexpr[] = "ID";
    ADSHANDLE hCI = 0;
    REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
            == openads::AE_SUCCESS);
    struct { double id; const char* data; } rows[] = {
        {10, "ten"}, {20, "twenty"}, {30, "thirty"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
        UNSIGNED8 fid[8] = "ID";
        UNSIGNED8 fd[8]  = "DATA";
        REQUIRE(AdsSetDouble(hC, fid, r.id) == openads::AE_SUCCESS);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
    }

    // Relate on the parent's CID expression -> child's numeric ID index.
    UNSIGNED8 rexpr[] = "CID";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);     // CID 30
    CHECK(child_data(hC) == "thirty");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CID 10
    CHECK(child_data(hC) == "ten");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CID 20
    CHECK(child_data(hC) == "twenty");

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}
