// M5 ADI create — AdiIndex::create, multi-tag bags, populated seek.
#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string trim_trailing_spaces(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void append_char_field(ADSHANDLE hTable, const char* field,
                       const char* value) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    UNSIGNED8 val[64]{};
    std::strncpy(reinterpret_cast<char*>(val), value, sizeof(val) - 1);
    REQUIRE(AdsSetString(hTable, fld, val,
                         static_cast<UNSIGNED32>(std::strlen(value)))
            == AE_SUCCESS);
}

void append_name_record(ADSHANDLE hTable, const char* name) {
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    append_char_field(hTable, "Name", name);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
}

void append_id_record(ADSHANDLE hTable, double id) {
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsSetDouble(hTable, fld, id) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
}

} // namespace

TEST_CASE("M5 AdsCreateIndex61 on ADT builds .adi and opens") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_abi_create";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "idxtest.adt", ec);
      fs::remove(tmp / "idxtest.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[] = "idxtest.adt";
    UNSIGNED8 flddef[] = "LandLordID,Character,25";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "idxtest.adi";
    UNSIGNED8 idxname[] = "LLID";
    UNSIGNED8 expr[]    = "LandLordID";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 0, &hIdx) == AE_SUCCESS);

    std::error_code ec;
    CHECK(fs::exists(tmp / "idxtest.adi", ec));
    CHECK(fs::file_size(tmp / "idxtest.adi", ec) == 3584u);

    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (tmp / "idxtest.adi").string());
    REQUIRE(tags);
    CHECK(tags.value().size() == 1u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADI add_tag: second index on same .adi") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_multitag";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "pairs.adt", ec);
      fs::remove(tmp / "pairs.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "pairs.adt";
    UNSIGNED8 flddef[] = "ID,Numeric,4;Code,Character,10";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "pairs.adi";
    ADSHANDLE hIdx1 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"ID",
                             (UNSIGNED8*)"ID", nullptr, nullptr, 0, 0, &hIdx1)
            == AE_SUCCESS);

    ADSHANDLE hIdx2 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"CODE",
                             (UNSIGNED8*)"Code", nullptr, nullptr, 0, 0, &hIdx2)
            == AE_SUCCESS);

    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (tmp / "pairs.adi").string());
    REQUIRE(tags);
    REQUIRE(tags.value().size() == 2u);

    std::error_code ec;
    CHECK(fs::file_size(tmp / "pairs.adi", ec) == 4608u);  // 6 + 3 pages

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADI create: populated index GotoTop/Skip in key order") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_populated";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "people.adt", ec);
      fs::remove(tmp / "people.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "people.adt";
    UNSIGNED8 flddef[] = "Name,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    append_name_record(hTable, "Carol");
    append_name_record(hTable, "Alice");
    append_name_record(hTable, "Bob");

    UNSIGNED8 idxfile[] = "people.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"NAME",
                             (UNSIGNED8*)"Name", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    std::vector<std::string> seen;
    for (;;) {
        UNSIGNED16 at_eof = 0;
        REQUIRE(AdsAtEOF(hTable, &at_eof) == AE_SUCCESS);
        if (at_eof) break;

        UNSIGNED8 fld[] = "Name";
        UNSIGNED8 buf[64]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, fld, buf, &len, 0) == AE_SUCCESS);
        seen.push_back(trim_trailing_spaces(
            std::string(reinterpret_cast<char*>(buf), len)));

        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }

    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "Alice");
    CHECK(seen[1] == "Bob");
    CHECK(seen[2] == "Carol");

    // Exact seek to middle key (space-padded to field width, same as index).
    std::string sk = "Bob";
    sk.append(20 - sk.size(), ' ');
    UNSIGNED8 seek_key[20]{};
    std::memcpy(seek_key, sk.data(), sk.size());
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, seek_key, 20, 0, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    UNSIGNED8 buf[64]{};
    UNSIGNED32 len = sizeof(buf);
    REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"Name", buf, &len, 0)
            == AE_SUCCESS);
    CHECK(trim_trailing_spaces(
              std::string(reinterpret_cast<char*>(buf), len)) == "Bob");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADI create: numeric index Skip in key order") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_numeric";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "nums.adt", ec);
      fs::remove(tmp / "nums.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "nums.adt";
    UNSIGNED8 flddef[] = "ID,Numeric,4";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    append_id_record(hTable, 30.0);
    append_id_record(hTable, 10.0);
    append_id_record(hTable, 20.0);

    UNSIGNED8 idxfile[] = "nums.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"ID",
                             (UNSIGNED8*)"ID", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    std::error_code ec;
    CHECK(fs::file_size(tmp / "nums.adi", ec) == 3072u);  // 6 pages

    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    std::vector<double> seen;
    for (;;) {
        UNSIGNED16 at_eof = 0;
        REQUIRE(AdsAtEOF(hTable, &at_eof) == AE_SUCCESS);
        if (at_eof) break;

        UNSIGNED8 fld[] = "ID";
        double dval = 0.0;
        REQUIRE(AdsGetDouble(hTable, fld, &dval) == AE_SUCCESS);
        seen.push_back(dval);

        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }

    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == doctest::Approx(10.0));
    CHECK(seen[1] == doctest::Approx(20.0));
    CHECK(seen[2] == doctest::Approx(30.0));

    double seek_val = 20.0;
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, reinterpret_cast<UNSIGNED8*>(&seek_val),
                    static_cast<UNSIGNED16>(sizeof(double)),
                    ADS_DOUBLEKEY, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    double got = 0.0;
    REQUIRE(AdsGetDouble(hTable, (UNSIGNED8*)"ID", &got) == AE_SUCCESS);
    CHECK(got == doctest::Approx(20.0));

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}