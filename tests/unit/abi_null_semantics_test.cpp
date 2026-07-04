#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

ADSHANDLE open_conn_null_semantics(const fs::path& dir) {
    fs::create_directories(dir);
    UNSIGNED8 srv[256]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);
    return hConn;
}

}  // namespace

TEST_CASE("AdsSetNull routes VFP nullable fields through _NullFlags") {
    const auto dir = fs::temp_directory_path() / "openads_vfp_setnull_flags";
    std::error_code ec;
    fs::remove_all(dir, ec);
    ADSHANDLE hConn = open_conn_null_semantics(dir);

    UNSIGNED8 def[] = "ID,AutoInc,4,0;NAME,Character,8,0,NULL;TAG,Character,4";
    UNSIGNED8 tbl[] = "vfpnull";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_VFP,
                           ADS_ANSI, 0, 0, 0, def, &hT) == AE_SUCCESS);

    UNSIGNED8 fName[] = "NAME";
    UNSIGNED8 fTag[] = "TAG";
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fName, (UNSIGNED8*)"ALPHA", 5) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fTag, (UNSIGNED8*)"BETA", 4) == AE_SUCCESS);

    UNSIGNED16 b = 0;
    REQUIRE(AdsIsNullable(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 1);
    REQUIRE(AdsIsNullable(hT, fTag, &b) == AE_SUCCESS);
    CHECK(b == 0);

    REQUIRE(AdsSetNull(hT, fName) == AE_SUCCESS);
    REQUIRE(AdsIsNull(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 1);
    REQUIRE(AdsIsEmpty(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 1);

    REQUIRE(AdsSetString(hT, fName, (UNSIGNED8*)"OMEGA", 5) == AE_SUCCESS);
    REQUIRE(AdsIsNull(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 0);
    REQUIRE(AdsIsEmpty(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 0);

    CHECK(AdsSetNull(hT, fTag) == AE_NOT_VFP_NULLABLE_FIELD);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetNull writes ADT native null sentinels") {
    const auto dir = fs::temp_directory_path() / "openads_adt_setnull_sentinel";
    std::error_code ec;
    fs::remove_all(dir, ec);
    ADSHANDLE hConn = open_conn_null_semantics(dir);

    UNSIGNED8 def[] = "ID,AutoInc,4;VAL,Integer,4;AMT,Double,8;NAME,Character,8";
    UNSIGNED8 tbl[] = "adtnull";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT,
                           ADS_ANSI, 0, 0, 0, def, &hT) == AE_SUCCESS);

    UNSIGNED8 fId[] = "ID";
    UNSIGNED8 fVal[] = "VAL";
    UNSIGNED8 fAmt[] = "AMT";
    UNSIGNED8 fName[] = "NAME";
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fVal, 123.0) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fAmt, 7.25) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fName, (UNSIGNED8*)"FILLED", 6) == AE_SUCCESS);

    UNSIGNED16 b = 0;
    REQUIRE(AdsIsNullable(hT, fVal, &b) == AE_SUCCESS);
    CHECK(b == 1);
    REQUIRE(AdsIsNullable(hT, fId, &b) == AE_SUCCESS);
    CHECK(b == 0);

    REQUIRE(AdsSetNull(hT, fVal) == AE_SUCCESS);
    REQUIRE(AdsSetNull(hT, fAmt) == AE_SUCCESS);
    REQUIRE(AdsSetNull(hT, fName) == AE_SUCCESS);
    REQUIRE(AdsIsNull(hT, fVal, &b) == AE_SUCCESS);
    CHECK(b == 1);
    REQUIRE(AdsIsNull(hT, fAmt, &b) == AE_SUCCESS);
    CHECK(b == 1);
    REQUIRE(AdsIsEmpty(hT, fName, &b) == AE_SUCCESS);
    CHECK(b == 1);

    REQUIRE(AdsSetDouble(hT, fVal, 456.0) == AE_SUCCESS);
    REQUIRE(AdsIsNull(hT, fVal, &b) == AE_SUCCESS);
    CHECK(b == 0);

    CHECK(AdsSetNull(hT, fId) == 5147);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
