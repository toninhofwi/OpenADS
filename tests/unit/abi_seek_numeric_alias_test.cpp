#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// F7 — dbSeek on a NUMERIC CDX tag whose key expression carries a
// workarea alias (`FIELD->ID`) must find existing keys. The write side
// strips the alias (strip_alias_qualifiers) and formats the key to the
// field width; the seek side resolved the field via the *unstripped*
// expression, so field_index() returned -1, the format width stayed at
// the stale 32-char key_length an INDEX-on-empty-table run produced,
// and the seek key never matched the stored key.
TEST_CASE("Seek numeric key on aliased CDX tag finds existing record") {
    auto dir = fs::temp_directory_path() / "openads_seek_num_alias";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,6,0;BAL,N,12,2";
    UNSIGNED8 tname[] = "acct";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // Create the index while the table is EMPTY → key_length defaults to
    // the stale 32, and use the aliased expression FIELD->ID.
    UNSIGNED8 bag[]  = "acct.cdx";
    UNSIGNED8 tag[]  = "BYID";
    UNSIGNED8 expr[] = "FIELD->ID";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Append three records AFTER the index exists.
    for (int id = 1001; id <= 1003; ++id) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fid[] = "ID";
        REQUIRE(AdsSetDouble(hT, fid, static_cast<double>(id)) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    // Seek the numeric key 1003 (ADS_DOUBLEKEY = raw double bytes).
    double dv = 1003.0;
    UNSIGNED8 key[sizeof(double)];
    std::memcpy(key, &dv, sizeof(double));
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI, key, sizeof(double), ADS_DOUBLEKEY,
                    /*soft=*/0, &found) == 0);

    INFO("found=" << found);
    CHECK(found == 1);

    UNSIGNED8 fid[] = "ID";
    double got = 0;
    REQUIRE(AdsGetDouble(hT, fid, &got) == 0);
    CHECK(got == doctest::Approx(1003.0));

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
