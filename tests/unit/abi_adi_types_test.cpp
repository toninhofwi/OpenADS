// M6 ADI — index create + order + seek for Date/Time/Timestamp/Money/Logical.
#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path types_tmp_dir() {
    return fs::temp_directory_path() / "openads_adi_types_m6";
}

void wipe_types_tmp() {
    std::error_code ec;
    fs::remove_all(types_tmp_dir(), ec);
    fs::create_directories(types_tmp_dir(), ec);
}

void connect_local(ADSHANDLE* hConn, const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, hConn)
            == AE_SUCCESS);
}

std::string trim_spaces(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void set_field_str(ADSHANDLE hTable, const char* field, const std::string& val) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    REQUIRE(AdsSetString(hTable, fld,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(val.data())),
                         static_cast<UNSIGNED32>(val.size()))
            == AE_SUCCESS);
}

std::string get_field_str(ADSHANDLE hTable, const char* field) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    UNSIGNED8 buf[512]{};
    UNSIGNED32 len = sizeof(buf);
    REQUIRE(AdsGetString(hTable, fld, buf, &len, 0) == AE_SUCCESS);
    return trim_spaces(std::string(reinterpret_cast<char*>(buf), len));
}

void create_index(ADSHANDLE hTable, const char* adi_name, const char* tag,
                  const char* expr, ADSHANDLE* hIdx) {
    UNSIGNED8 idxfile[64]{};
    UNSIGNED8 idxname[64]{};
    UNSIGNED8 idxexpr[64]{};
    std::strncpy(reinterpret_cast<char*>(idxfile), adi_name, sizeof(idxfile) - 1);
    std::strncpy(reinterpret_cast<char*>(idxname), tag, sizeof(idxname) - 1);
    std::strncpy(reinterpret_cast<char*>(idxexpr), expr, sizeof(idxexpr) - 1);
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, idxexpr,
                             nullptr, nullptr, 0, 0, hIdx)
            == AE_SUCCESS);
}

std::vector<std::string> walk_tag_column(ADSHANDLE hTable, const char* field) {
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    std::vector<std::string> seen;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        seen.push_back(get_field_str(hTable, field));
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    return seen;
}

bool seek_double_key(ADSHANDLE hIdx, double key) {
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, reinterpret_cast<UNSIGNED8*>(&key),
                    static_cast<UNSIGNED16>(sizeof(double)),
                    ADS_DOUBLEKEY, 0, &found) == AE_SUCCESS);
    return found != 0;
}

bool seek_raw_key(ADSHANDLE hIdx, const std::string& key8) {
    REQUIRE(key8.size() == 8u);
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx,
                    reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key8.data())),
                    8, ADS_RAWKEY, 0, &found) == AE_SUCCESS);
    return found != 0;
}

std::uint32_t ymd_to_jdn(int y, int m, int d) {
    const std::int64_t a  = (14 - m) / 12;
    const std::int64_t yy = y + 4800 - a;
    const std::int64_t mm = m + 12 * a - 3;
    return static_cast<std::uint32_t>(
        d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045);
}

} // namespace

TEST_CASE("M6 ADI index: Date field order and seek") {
    wipe_types_tmp();
    auto dir = types_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "dates.adt";
    UNSIGNED8 flddef[] = "Tag,Character,8;Nasc,Date,8";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    auto row = [&](const char* tag, const char* nasc) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        set_field_str(hTable, "Tag", tag);
        set_field_str(hTable, "Nasc", nasc);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    row("c", "20000615");
    row("a", "19991231");
    row("b", "19850315");

    ADSHANDLE hIdx = 0;
    create_index(hTable, "dates.adi", "NASC", "Nasc", &hIdx);

    auto seen = walk_tag_column(hTable, "Nasc");
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "19850315");
    CHECK(seen[1] == "19991231");
    CHECK(seen[2] == "20000615");

    const double seek_jdn = static_cast<double>(ymd_to_jdn(1999, 12, 31));
    CHECK(seek_double_key(hIdx, seek_jdn));
    CHECK(get_field_str(hTable, "Tag") == "a");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M6 ADI index: Time field order and seek") {
    wipe_types_tmp();
    auto dir = types_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "times.adt";
    UNSIGNED8 flddef[] = "Tag,Character,8;Hora,Time,4";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    auto row = [&](const char* tag, double ms) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        set_field_str(hTable, "Tag", tag);
        UNSIGNED8 fld[] = "Hora";
        REQUIRE(AdsSetDouble(hTable, fld, ms) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    row("noon", 52200000.0);
    row("one",  3600000.0);
    row("zero", 0.0);

    ADSHANDLE hIdx = 0;
    create_index(hTable, "times.adi", "HORA", "Hora", &hIdx);

    auto seen = walk_tag_column(hTable, "Tag");
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "zero");
    CHECK(seen[1] == "one");
    CHECK(seen[2] == "noon");

    CHECK(seek_double_key(hIdx, 3600000.0));
    CHECK(get_field_str(hTable, "Tag") == "one");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M6 ADI index: Timestamp field order and seek") {
    wipe_types_tmp();
    auto dir = types_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "tstamps.adt";
    UNSIGNED8 flddef[] = "Tag,Character,8;Criado,Timestamp";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    auto row = [&](const char* tag, const char* ts) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        set_field_str(hTable, "Tag", tag);
        set_field_str(hTable, "Criado", ts);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    row("mid",  "20251225120000");
    row("early","20250621143000");
    row("old",  "19990101120000");

    ADSHANDLE hIdx = 0;
    create_index(hTable, "tstamps.adi", "CRIADO", "Criado", &hIdx);

    auto seen = walk_tag_column(hTable, "Criado");
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "19990101120000");
    CHECK(seen[1] == "20250621143000");
    CHECK(seen[2] == "20251225120000");

    const std::uint32_t jdn = ymd_to_jdn(2025, 6, 21);
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(jdn) << 32) |
        static_cast<std::uint64_t>(52200000u);
    CHECK(seek_raw_key(hIdx, openads::drivers::adi::pack_u64_key(packed)));
    CHECK(get_field_str(hTable, "Tag") == "early");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M6 ADI index: Money field order and seek") {
    wipe_types_tmp();
    auto dir = types_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "money.adt";
    UNSIGNED8 flddef[] = "Tag,Character,8;Saldo,Money";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    auto row = [&](const char* tag, double amount) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        set_field_str(hTable, "Tag", tag);
        UNSIGNED8 fld[] = "Saldo";
        REQUIRE(AdsSetDouble(hTable, fld, amount) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    row("hi",  300.50);
    row("lo",  100.00);
    row("mid", 200.25);

    ADSHANDLE hIdx = 0;
    create_index(hTable, "money.adi", "SALDO", "Saldo", &hIdx);

    auto seen = walk_tag_column(hTable, "Tag");
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "lo");
    CHECK(seen[1] == "mid");
    CHECK(seen[2] == "hi");

    const double raw_key = 200.25 * 10000.0;
    CHECK(seek_double_key(hIdx, raw_key));
    CHECK(get_field_str(hTable, "Tag") == "mid");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M6 ADI index: Logical field order and seek") {
    wipe_types_tmp();
    auto dir = types_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "logic.adt";
    UNSIGNED8 flddef[] = "Tag,Character,8;Ativo,Logical";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    auto row = [&](const char* tag, UNSIGNED16 val) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        set_field_str(hTable, "Tag", tag);
        UNSIGNED8 fld[] = "Ativo";
        REQUIRE(AdsSetLogical(hTable, fld, val) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    row("t1", 1);
    row("f",  0);
    row("t2", 1);

    ADSHANDLE hIdx = 0;
    create_index(hTable, "logic.adi", "ATIVO", "Ativo", &hIdx);

    auto seen = walk_tag_column(hTable, "Tag");
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "f");
    CHECK(seen[1] == "t1");
    CHECK(seen[2] == "t2");

    CHECK(seek_double_key(hIdx, 0.0));
    CHECK(get_field_str(hTable, "Tag") == "f");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}