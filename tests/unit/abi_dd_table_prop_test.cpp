#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "session/connection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Minimal DBF so AdsOpenTable succeeds in the existing DD tests.
fs::path make_dbf_for_prop(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03; hdr[4] = 0;
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Minimal ADT file (400-byte header + one CHAR(4) field, 0 records).
fs::path make_adt_for_prop(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    const std::uint32_t HDR_LEN = 600;
    const std::uint32_t REC_LEN = 9;
    std::vector<std::uint8_t> file(HDR_LEN, 0);
    auto put_u32 = [&](std::size_t off, std::uint32_t v) {
        file[off+0] = v & 0xFF; file[off+1] = (v>>8) & 0xFF;
        file[off+2] = (v>>16) & 0xFF; file[off+3] = (v>>24) & 0xFF;
    };
    auto put_u16 = [&](std::size_t off, std::uint16_t v) {
        file[off+0] = v & 0xFF; file[off+1] = (v>>8) & 0xFF;
    };
    std::memcpy(file.data(), "Advantage Table", 15);
    put_u32(24, 0); put_u32(32, HDR_LEN); put_u32(36, REC_LEN);
    std::memcpy(file.data() + 400, "TAG", 3);
    put_u16(400 + 129, 4); put_u16(400 + 131, 5); put_u16(400 + 135, 4);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::vector<std::string> table_prop_sql_col(ADSHANDLE hConn,
                                            const char* sql,
                                            const char* field_name) {
    std::vector<std::string> out;
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return out;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return out;
    }
    UNSIGNED8 fname[64];
    std::memcpy(fname, field_name, std::strlen(field_name) + 1);
    AdsGotoTop(cur);
    for (int i = 0; i < 20; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(cur, &eof);
        if (eof) break;
        UNSIGNED8 vbuf[256] = {};
        UNSIGNED32 vlen = sizeof(vbuf) - 1;
        AdsGetField(cur, fname, vbuf, &vlen, 0);
        std::string s(reinterpret_cast<const char*>(vbuf), vlen);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        out.push_back(s);
        AdsSkip(cur, 1);
    }
    AdsCloseTable(cur);
    AdsCloseSQLStatement(stmt);
    return out;
}

} // namespace

TEST_CASE("AdsDDGetTableProperty — RELATIVE_PATH and TYPE for DBF table") {
    const auto dir = fs::temp_directory_path() / "openads_dd_tprop_dbf";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_for_prop(dir, "clients.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);

    UNSIGNED8 alias[64] = "clients";
    UNSIGNED8 path [64] = "clients.dbf";
    REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);

    // RELATIVE_PATH (211)
    UNSIGNED8 buf[512] = {0};
    UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetTableProperty(hConn, alias,
                                  ADS_DD_TABLE_RELATIVE_PATH,
                                  buf, &len) == 0);
    std::string rel(reinterpret_cast<const char*>(buf), len);
    CHECK(rel == "clients.dbf");

    // TABLE_TYPE (204) — DBF → ADS_CDX = 2
    UNSIGNED16 ttype = 0xFFFF;
    len = sizeof(ttype);
    REQUIRE(AdsDDGetTableProperty(hConn, alias,
                                  ADS_DD_TABLE_TYPE,
                                  &ttype, &len) == 0);
    CHECK(len == 2u);
    CHECK(ttype == ADS_CDX);

    // CHAR_TYPE (212) — always ANSI
    UNSIGNED16 ctype = 0xFFFF;
    len = sizeof(ctype);
    REQUIRE(AdsDDGetTableProperty(hConn, alias,
                                  ADS_DD_TABLE_CHAR_TYPE,
                                  &ctype, &len) == 0);
    CHECK(len == 2u);
    CHECK(ctype == ADS_ANSI);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDGetTableProperty — TABLE_PATH for ADT table") {
    const auto dir = fs::temp_directory_path() / "openads_dd_tprop_adt";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_adt_for_prop(dir, "props.adt");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);

    UNSIGNED8 alias[64] = "props";
    UNSIGNED8 path [64] = "props.adt";
    REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);

    // TABLE_TYPE (204) — ADT → ADS_ADT = 3
    UNSIGNED16 ttype = 0;
    UNSIGNED16 len = sizeof(ttype);
    REQUIRE(AdsDDGetTableProperty(hConn, alias,
                                  ADS_DD_TABLE_TYPE,
                                  &ttype, &len) == 0);
    CHECK(ttype == ADS_ADT);

    // TABLE_PATH (205) — should contain the dir + "props.adt"
    UNSIGNED8 pathbuf[512] = {0};
    len = sizeof(pathbuf);
    REQUIRE(AdsDDGetTableProperty(hConn, alias,
                                  ADS_DD_TABLE_PATH,
                                  pathbuf, &len) == 0);
    std::string abs_path(reinterpret_cast<const char*>(pathbuf), len);
    // Must contain the table filename.
    CHECK(abs_path.find("props.adt") != std::string::npos);
    // Must be longer than just the relative part (i.e. it was prefixed with dir).
    CHECK(abs_path.size() > std::string("props.adt").size());

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDGetTableProperty — unknown alias returns error") {
    const auto dir = fs::temp_directory_path() / "openads_dd_tprop_miss";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);

    UNSIGNED8 noalias[64] = "doesnotexist";
    UNSIGNED8 outbuf[256] = {0};
    UNSIGNED16 len = sizeof(outbuf);
    UNSIGNED32 r = AdsDDGetTableProperty(hConn, noalias,
                                         ADS_DD_TABLE_RELATIVE_PATH,
                                         outbuf, &len);
    CHECK(r != 0);  // must fail — alias not in DD

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDGetTableProperty — unknown property code returns error") {
    const auto dir = fs::temp_directory_path() / "openads_dd_tprop_unk";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_for_prop(dir, "x.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
    UNSIGNED8 alias[64] = "x";
    UNSIGNED8 path [64] = "x.dbf";
    REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);

    UNSIGNED8 outbuf[32] = {0};
    UNSIGNED16 len = sizeof(outbuf);
    UNSIGNED32 r = AdsDDGetTableProperty(hConn, alias,
                                         0xFFFF, outbuf, &len);
    CHECK(r != 0);  // unknown property code

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetTableProperty returns AE_FUNCTION_NOT_AVAILABLE") {
    const auto dir = fs::temp_directory_path() / "openads_dd_setprop";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_for_prop(dir, "y.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
    UNSIGNED8 alias[64] = "y";
    UNSIGNED8 path [64] = "y.dbf";
    REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);

    UNSIGNED8 val[4] = {0};
    UNSIGNED32 r = AdsDDSetTableProperty(hConn, alias,
                                         ADS_DD_TABLE_TYPE, val, 2);
    CHECK(r == openads::AE_FUNCTION_NOT_AVAILABLE);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetTableProperty stores table memory/cache options") {
    const auto dir = fs::temp_directory_path() / "openads_dd_table_cache_props";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_adt_for_prop(dir, "cacheme.adt");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
    UNSIGNED8 alias[64] = "cacheme";
    UNSIGNED8 path [64] = "cacheme.adt";
    REQUIRE(AdsDDAddTable(hConn, alias, path, ADS_ADT, 0, nullptr, nullptr) == 0);

    std::uint16_t on = 1;
    std::uint16_t memo = 1024;
    std::uint16_t cache = ADS_TABLE_CACHE_READS;
    REQUIRE(AdsDDSetTableProperty(hConn, alias, ADS_DD_TABLE_AUTO_CREATE, &on, 2) == 0);
    REQUIRE(AdsDDSetTableProperty(hConn, alias, ADS_DD_TABLE_MEMO_BLOCK_SIZE, &memo, 2) == 0);
    REQUIRE(AdsDDSetTableProperty(hConn, alias, ADS_DD_TABLE_CACHING, &cache, 2) == 0);

    UNSIGNED16 len = 2;
    std::uint8_t out[2] = {};
    REQUIRE(AdsDDGetTableProperty(hConn, alias, ADS_DD_TABLE_AUTO_CREATE, out, &len) == 0);
    CHECK(len == 2);
    CHECK(out[0] == 1);
    len = 2;
    REQUIRE(AdsDDGetTableProperty(hConn, alias, ADS_DD_TABLE_MEMO_BLOCK_SIZE, out, &len) == 0);
    CHECK(static_cast<unsigned>(out[0] | (out[1] << 8)) == 1024u);
    len = 2;
    REQUIRE(AdsDDGetTableProperty(hConn, alias, ADS_DD_TABLE_CACHING, out, &len) == 0);
    CHECK(out[0] == ADS_TABLE_CACHE_READS);

    auto auto_vals = table_prop_sql_col(
        hConn,
        "SELECT Table_Auto_Create FROM system.tables WHERE Name = 'cacheme'",
        "Table_Auto_Create");
    auto memo_vals = table_prop_sql_col(
        hConn,
        "SELECT Table_Memo_Block_Size FROM system.tables WHERE Name = 'cacheme'",
        "Table_Memo_Block_Size");
    auto cache_vals = table_prop_sql_col(
        hConn,
        "SELECT Table_Caching FROM system.tables WHERE Name = 'cacheme'",
        "Table_Caching");
    REQUIRE(auto_vals.size() == 1);
    REQUIRE(memo_vals.size() == 1);
    REQUIRE(cache_vals.size() == 1);
    CHECK(auto_vals[0] == "True");
    CHECK(memo_vals[0] == "1024");
    CHECK(cache_vals[0] == "1");

    REQUIRE(AdsDisconnect(hConn) == 0);

    hConn = 0;
    REQUIRE(AdsConnect60(add_buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    len = 2;
    out[0] = out[1] = 0;
    REQUIRE(AdsDDGetTableProperty(hConn, alias, ADS_DD_TABLE_CACHING, out, &len) == 0);
    CHECK(out[0] == ADS_TABLE_CACHE_READS);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("DD table caching property wraps opened table safely") {
    const auto dir = fs::temp_directory_path() / "openads_dd_table_cache_open";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_adt_for_prop(dir, "hotref.adt");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
    UNSIGNED8 alias[64] = "hotref";
    UNSIGNED8 path [64] = "hotref.adt";
    REQUIRE(AdsDDAddTable(hConn, alias, path, ADS_ADT, 0, nullptr, nullptr) == 0);
    std::uint16_t cache = ADS_TABLE_CACHE_READS;
    REQUIRE(AdsDDSetTableProperty(hConn, alias, ADS_DD_TABLE_CACHING, &cache, 2) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    auto conn_r = openads::session::Connection::open(add_path);
    REQUIRE(conn_r.has_value());
    auto conn = std::move(conn_r).value();
    auto h = conn.open_table("hotref", openads::engine::TableType::Adt,
                             openads::engine::OpenMode::Shared);
    REQUIRE(h.has_value());
    auto* t = conn.lookup_table(h.value());
    REQUIRE(t != nullptr);
    CHECK(t->cache_enabled());

    REQUIRE(t->append_record().has_value());
    REQUIRE(t->flush().has_value());
    conn.close_table(h.value());

    auto raw = openads::engine::Table::open((dir / "hotref.adt").string(),
                                            openads::engine::TableType::Adt);
    REQUIRE(raw.has_value());
    CHECK(raw.value().record_count() == 1);
    fs::remove_all(dir, ec);
}
