// M12.22 — versioned ACE overloads bound by name from the X# RDD.
// Exercises the forwarding shims (Connect26 / CreateTable90 /
// OpenTable90 / Reindex61) plus the minimal bookmark / property /
// find-by-name implementations.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("M12.22 versioned ACE overloads") {
    const auto dir = fs::temp_directory_path() / "openads_m1222_overloads";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect26(srv, ADS_LOCAL_SERVER, &hConn) == openads::AE_SUCCESS);
    REQUIRE(hConn != 0);

    UNSIGNED8 name[64]   = "vtab";
    UNSIGNED8 alias[64]  = "vtab";
    UNSIGNED8 fields[128] = "ID,Numeric,5,0;NAME,Character,10";
    UNSIGNED8 collation[8] = "";

    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable90(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                             fields, 0, collation, &hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);

    hTable = 0;
    REQUIRE(AdsOpenTable90(hConn, name, alias, ADS_CDX, 0, 0, 0, 0,
                           collation, &hTable) == openads::AE_SUCCESS);
    REQUIRE(hTable != 0);

    // Two records so a bookmark roundtrip has somewhere to jump.
    UNSIGNED8 fID[8]   = "ID";
    UNSIGNED8 fNAME[8] = "NAME";
    REQUIRE(AdsAppendRecord(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fID,   (UNSIGNED8*)"1",   1) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fNAME, (UNSIGNED8*)"row1", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fID,   (UNSIGNED8*)"2",   1) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fNAME, (UNSIGNED8*)"row2", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == openads::AE_SUCCESS);

    SUBCASE("AdsGetBookmark60 / AdsGotoBookmark60 roundtrip") {
        REQUIRE(AdsGotoTop(hTable) == openads::AE_SUCCESS);
        UNSIGNED32 rec1 = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rec1) == openads::AE_SUCCESS);

        std::array<UNSIGNED8, 32> bm{};
        UNSIGNED32 bmlen = static_cast<UNSIGNED32>(bm.size());
        REQUIRE(AdsGetBookmark60(hTable, bm.data(), &bmlen) == openads::AE_SUCCESS);
        CHECK(bmlen == 4u);

        REQUIRE(AdsGotoBottom(hTable) == openads::AE_SUCCESS);
        UNSIGNED32 recBottom = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &recBottom) == openads::AE_SUCCESS);
        CHECK(recBottom != rec1);

        REQUIRE(AdsGotoBookmark60(hTable, bm.data(), bmlen) == openads::AE_SUCCESS);
        UNSIGNED32 recBack = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &recBack) == openads::AE_SUCCESS);
        CHECK(recBack == rec1);
    }

    SUBCASE("AdsGetBookmark60 rejects an undersized buffer") {
        UNSIGNED8 tiny[2] = {0, 0};
        UNSIGNED32 cap = 2;
        CHECK(AdsGetBookmark60(hTable, tiny, &cap) != openads::AE_SUCCESS);
        CHECK(cap == 4u);  // reports the size it needs
    }

    SUBCASE("forwards return success") {
        UNSIGNED16 exact = 99;
        CHECK(AdsGetExact22(hTable, &exact) == openads::AE_SUCCESS);

        UNSIGNED8 fmt[16] = "";
        UNSIGNED16 fmtlen = sizeof(fmt);
        CHECK(AdsGetDateFormat60(hConn, fmt, &fmtlen) == openads::AE_SUCCESS);

        CHECK(AdsReindex61(hTable, 8192) == openads::AE_SUCCESS);
        CHECK(AdsCancelUpdate90(hTable, 0) == openads::AE_SUCCESS);

        UNSIGNED64 v = 0;
        CHECK(AdsSetProperty90(hTable, 1, &v) == openads::AE_SUCCESS);
    }

    SUBCASE("100/90 compatibility wrappers forward to base APIs") {
        UNSIGNED8 filter[] = "ID = 1";
        CHECK(AdsSetFilter100(hTable, filter, 0) == openads::AE_SUCCESS);

        UNSIGNED16 opt = 99;
        CHECK(AdsEvalAOF100(hTable, filter, 0, &opt) == openads::AE_SUCCESS);
        CHECK(opt == 0);

        UNSIGNED8 aof[64] = "";
        UNSIGNED16 aof_len = sizeof(aof);
        CHECK(AdsGetAOF100(hTable, aof, &aof_len, 0) == openads::AE_SUCCESS);

        opt = 99;
        aof_len = sizeof(aof);
        CHECK(AdsGetAOFOptLevel100(hTable, &opt, aof, &aof_len, 0)
              == openads::AE_SUCCESS);

        CHECK(AdsMgKillUser90(0, nullptr, 0, 0)
              == openads::AE_INVALID_CONNECTION_HANDLE);
    }

    SUBCASE("additional SAP compatibility wrappers expose existing state") {
        CHECK(AdsSetDecimals(7) == openads::AE_SUCCESS);
        UNSIGNED16 decimals = 0;
        CHECK(AdsGetDecimals(&decimals) == openads::AE_SUCCESS);
        CHECK(decimals == 7);

        UNSIGNED16 trans_free = 99;
        CHECK(AdsIsTableTransactionFree(hTable, &trans_free)
              == openads::AE_SUCCESS);
        CHECK(trans_free == 0);
        CHECK(AdsSetTableTransactionFree(hTable, 1) == openads::AE_SUCCESS);
        CHECK(AdsIsTableTransactionFree(hTable, &trans_free)
              == openads::AE_SUCCESS);
        CHECK(trans_free == 1);

        CHECK(AdsGotoBOF(hTable) == openads::AE_SUCCESS);
        UNSIGNED16 at_bof = 0;
        UNSIGNED16 at_eof = 0;
        CHECK(AdsAtBOF(hTable, &at_bof) == openads::AE_SUCCESS);
        CHECK(AdsAtEOF(hTable, &at_eof) == openads::AE_SUCCESS);
        CHECK(at_bof == 1);
        CHECK(at_eof == 0);

        CHECK(AdsGotoEOF(hTable) == openads::AE_SUCCESS);
        CHECK(AdsAtBOF(hTable, &at_bof) == openads::AE_SUCCESS);
        CHECK(AdsAtEOF(hTable, &at_eof) == openads::AE_SUCCESS);
        CHECK(at_bof == 0);
        CHECK(at_eof == 1);
    }

    SUBCASE("by-name lookups report not-found rather than crash") {
        ADSHANDLE h = 1234;
        UNSIGNED8 path[8] = "x";
        CHECK(AdsFindConnection25(path, &h) == openads::AE_NO_CONNECTION);
        CHECK(h == 0);

        h = 1234;
        UNSIGNED8 tname[8] = "vtab";
        CHECK(AdsGetTableHandle25(hConn, tname, &h) == openads::AE_TABLE_NOT_FOUND);
        CHECK(h == 0);

        h = 1234;
        CHECK(AdsGetTableHandle(hConn, tname, &h) == openads::AE_TABLE_NOT_FOUND);
        CHECK(h == 0);
    }

    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsConnect101 parses documented connection strings") {
    const auto dir = fs::temp_directory_path() / "openads_connect101";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    std::string conn =
        " Data Source = '" + dir.string() + "' ; "
        " ServerType = LOCAL ; "
        " TableType = ADS_CDX ; "
        " ReadOnly = TRUE ; "
        " Decimals = 6 ; "
        " Exact = TRUE ; "
        " SQLTimeout = 9 ; ";

    ADSHANDLE hOptions = 1234;
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect101(reinterpret_cast<UNSIGNED8*>(conn.data()),
                          &hOptions, &hConn) == openads::AE_SUCCESS);
    CHECK(hConn != 0);
    REQUIRE(hOptions != 0);

    UNSIGNED32 option_count = 0;
    CHECK(AdsGetRecordCount(hOptions, 0, &option_count) == openads::AE_SUCCESS);
    CHECK(option_count == 7);
    std::map<std::string, std::string> parsed_options;
    REQUIRE(AdsGotoTop(hOptions) == openads::AE_SUCCESS);
    UNSIGNED8 option_name_field[] = "Name";
    UNSIGNED8 option_value_field[] = "Value";
    for (UNSIGNED32 i = 0; i < option_count; ++i) {
        char key[128] = {};
        UNSIGNED32 key_len = sizeof(key);
        char value[1200] = {};
        UNSIGNED32 value_len = sizeof(value);
        CHECK(AdsGetString(hOptions, option_name_field,
                           reinterpret_cast<UNSIGNED8*>(key), &key_len,
                           0) == openads::AE_SUCCESS);
        CHECK(AdsGetString(hOptions, option_value_field,
                           reinterpret_cast<UNSIGNED8*>(value), &value_len,
                           0) == openads::AE_SUCCESS);
        parsed_options[key] = value;
        if (i + 1 < option_count) {
            CHECK(AdsSkip(hOptions, 1) == openads::AE_SUCCESS);
        }
    }
    CHECK(parsed_options["datasource"] == dir.string());
    CHECK(parsed_options["readonly"] == "TRUE");
    CHECK(parsed_options["tabletype"] == "ADS_CDX");
    CHECK(AdsCloseTable(hOptions) == openads::AE_SUCCESS);

    UNSIGNED16 decimals = 0;
    CHECK(AdsGetDecimals(&decimals) == openads::AE_SUCCESS);
    CHECK(decimals == 6);

    UNSIGNED16 exact = 0;
    CHECK(AdsGetExact(&exact) == openads::AE_SUCCESS);
    CHECK(exact == 1);

    UNSIGNED8 name[32] = "open101";
    UNSIGNED8 fields[64] = "ID,Numeric,5,0";
    ADSHANDLE hCreated = 0;
    CHECK(AdsCreateTable(hConn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_DEFAULT, ADS_DEFAULT, 64, fields, &hCreated)
          == openads::AE_SUCCESS);
    REQUIRE(hCreated != 0);
    CHECK(AdsCloseTable(hCreated) == openads::AE_SUCCESS);

    ADSHANDLE hOpened = 0;
    CHECK(AdsOpenTable101(hConn, name, &hOpened) == openads::AE_SUCCESS);
    REQUIRE(hOpened != 0);
    UNSIGNED16 table_type = 0;
    CHECK(AdsGetTableType(hOpened, &table_type) == openads::AE_SUCCESS);
    CHECK(table_type == ADS_CDX);
    UNSIGNED32 open_options = 0;
    CHECK(AdsGetTableOpenOptions(hOpened, &open_options)
          == openads::AE_SUCCESS);
    CHECK(open_options == ADS_READONLY);
    CHECK(AdsCloseTable(hOpened) == openads::AE_SUCCESS);

    CHECK(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    ADSHANDLE missing = 99;
    UNSIGNED8 bad[] = "ServerType=LOCAL";
    CHECK(AdsConnect101(bad, nullptr, &missing) != openads::AE_SUCCESS);
    CHECK(missing == 0);

    fs::remove_all(dir, ec);
}
