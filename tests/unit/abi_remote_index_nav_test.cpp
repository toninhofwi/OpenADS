// M12.23 — rddads passes hOrdCurrent (a RemoteIndex handle) to
// AdsGotoTop / AdsSkip after AdsCreateIndex61 over TCP. Without
// routing those calls through the parent table cursor the post-create
// DBGoTop hangs or returns AE_INTERNAL_ERROR.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void make_colonias_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] =
        "COLONIA,C,20,0;NOMBRE,C,30,0;CP,C,5,0";
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct Row { const char* col; const char* nom; const char* cp; };
    const Row rows[] = {
        {"Centro",   "Av. Hidalgo", "06000"},
        {"Roma",     "Calle Orizaba", "06700"},
        {"Condesa",  "Av. Amsterdam", "06140"},
    };
    UNSIGNED8 f_col[] = "COLONIA";
    UNSIGNED8 f_nom[] = "NOMBRE";
    UNSIGNED8 f_cp[]  = "CP";
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, f_col, (UNSIGNED8*)r.col,
                     static_cast<UNSIGNED32>(std::strlen(r.col)));
        AdsSetString(hT, f_nom, (UNSIGNED8*)r.nom,
                     static_cast<UNSIGNED32>(std::strlen(r.nom)));
        AdsSetString(hT, f_cp, (UNSIGNED8*)r.cp,
                     static_cast<UNSIGNED32>(std::strlen(r.cp)));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

} // namespace

TEST_CASE("remote AdsGotoTop/AdsSkip accept RemoteIndex handles") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_idx_nav";
    make_colonias_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "CCOLONIA.CDX";
    UNSIGNED8 tag[]  = "COLONIA";
    UNSIGNED8 expr[] = "COLONIA";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512,
                             &hIndex) == 0);
    REQUIRE(hIndex != 0);

    UNSIGNED8 nm[32] = {0};
    UNSIGNED16 nl = sizeof(nm);
    REQUIRE(AdsGetIndexName(hIndex, nm, &nl) == 0);
    CHECK(std::string(reinterpret_cast<char*>(nm), nl) == "COLONIA");

    // Simulate rddads: navigation via the index handle, not the table.
    REQUIRE(AdsGotoTop(hIndex) == 0);

    UNSIGNED32 r1 = 0, r2 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r1) == 0);
    CHECK(r1 > 0u);

    REQUIRE(AdsSkip(hIndex, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r2) == 0);
    CHECK(r2 != r1);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote legacy AdsCreateIndex routes to AdsCreateIndex61") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_idx_legacy";
    make_colonias_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "CCOLONIA.CDX";
    UNSIGNED8 tag[]  = "NOMBRE";
    UNSIGNED8 expr[] = "NOMBRE";
    REQUIRE(AdsCreateIndex(hTable, bag, tag, expr,
                           nullptr, ADS_COMPOUND, 512, &hIndex) == 0);
    REQUIRE(hIndex != 0);
    REQUIRE(AdsGotoTop(hIndex) == 0);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}