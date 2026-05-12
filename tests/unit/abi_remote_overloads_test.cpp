// M12.22 — versioned ACE overloads exercised over the wire against a
// live openads_serverd. Gated on OPENADS_TEST_REMOTE: set it to the
// server URI (e.g. "tcp://host:16262/") whose data dir holds
// customer.dbf, otherwise this case is skipped.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
const char* remote_uri_env() { return std::getenv("OPENADS_TEST_REMOTE"); }
const char* remote_big_env() { return std::getenv("OPENADS_TEST_REMOTE_BIG"); }

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

TEST_CASE("M12.22 versioned overloads over the wire"
          * doctest::skip(remote_uri_env() == nullptr)) {
    const std::string uri = remote_uri_env();
    std::array<UNSIGNED8, 512> srv{};
    REQUIRE(uri.size() < srv.size());
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect26(srv.data(), ADS_REMOTE_SERVER, &hConn)
            == openads::AE_SUCCESS);
    REQUIRE(hConn != 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[64] = "customer.dbf";
    UNSIGNED8 alias[64] = "cust";
    UNSIGNED8 coll[8]   = "";
    REQUIRE(AdsOpenTable90(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0,
                           coll, &hTable) == openads::AE_SUCCESS);
    REQUIRE(hTable != 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == openads::AE_SUCCESS);
    CHECK(cnt > 0u);

    SUBCASE("AdsGetBookmark60 / AdsGotoBookmark60 roundtrip over the wire") {
        REQUIRE(AdsGotoTop(hTable) == openads::AE_SUCCESS);
        UNSIGNED32 rtop = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rtop) == openads::AE_SUCCESS);

        std::array<UNSIGNED8, 16> bm{};
        UNSIGNED32 bml = static_cast<UNSIGNED32>(bm.size());
        REQUIRE(AdsGetBookmark60(hTable, bm.data(), &bml) == openads::AE_SUCCESS);
        CHECK(bml == 4u);

        REQUIRE(AdsGotoBottom(hTable) == openads::AE_SUCCESS);
        UNSIGNED32 rbot = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rbot) == openads::AE_SUCCESS);
        if (cnt > 1u) CHECK(rbot != rtop);

        REQUIRE(AdsGotoBookmark60(hTable, bm.data()) == openads::AE_SUCCESS);
        UNSIGNED32 rback = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rback) == openads::AE_SUCCESS);
        CHECK(rback == rtop);
    }

    SUBCASE("forward shims return success against a remote handle") {
        UNSIGNED16 ex = 9;
        CHECK(AdsGetExact22(hTable, &ex) == openads::AE_SUCCESS);
        UNSIGNED8 fmt[16] = "";
        UNSIGNED16 fmtlen = sizeof(fmt);
        CHECK(AdsGetDateFormat60(hConn, fmt, &fmtlen) == openads::AE_SUCCESS);
        CHECK(AdsCancelUpdate90(hTable, 0) == openads::AE_SUCCESS);
        UNSIGNED64 v = 0;
        CHECK(AdsSetProperty90(hTable, 1, &v) == openads::AE_SUCCESS);
    }

    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
}

// Big-table wire smoke. Gated on OPENADS_TEST_REMOTE (server URI) +
// OPENADS_TEST_REMOTE_BIG (the table name, e.g. "big.dbf") whose data
// dir holds a ~1 GB DBF. Opens it, walks/jumps over the wire, prints
// timings.
TEST_CASE("remote 1 GB DBF over the wire"
          * doctest::skip(remote_uri_env() == nullptr || remote_big_env() == nullptr)) {
    using clk = std::chrono::steady_clock;
    const std::string uri = remote_uri_env();
    std::array<UNSIGNED8, 512> srv{};
    REQUIRE(uri.size() < srv.size());
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);
    REQUIRE(hConn != 0);

    ADSHANDLE hTable = 0;
    std::array<UNSIGNED8, 256> tname{};
    {
        const std::string t = remote_big_env();
        REQUIRE(t.size() < tname.size());
        std::memcpy(tname.data(), t.c_str(), t.size() + 1);
    }
    UNSIGNED8 alias[8] = "big";
    {
        auto t0 = clk::now();
        REQUIRE(AdsOpenTable(hConn, tname.data(), alias, ADS_CDX, 0, 0, 0, 0, &hTable)
                == openads::AE_SUCCESS);
        std::printf("[1GB] open: %.1f ms\n", ms_since(t0));
    }
    REQUIRE(hTable != 0);

    UNSIGNED32 cnt = 0;
    {
        auto t0 = clk::now();
        REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == openads::AE_SUCCESS);
        std::printf("[1GB] RecCount = %u  (%.1f ms)\n", cnt, ms_since(t0));
    }
    CHECK(cnt > 1000000u);   // we generated ~5 M records

    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == openads::AE_SUCCESS);
    CHECK(nflds == 5);

    auto read_field1 = [&](const char* what) {
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 cap = sizeof(buf);
        UNSIGNED8 f[8] = "ID";
        REQUIRE(AdsGetField(hTable, f, buf, &cap, 0) == openads::AE_SUCCESS);
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == openads::AE_SUCCESS);
        std::printf("[1GB] %s -> recno=%u  ID=[%.*s]\n", what, rec,
                    static_cast<int>(cap), reinterpret_cast<char*>(buf));
        return rec;
    };

    { auto t0 = clk::now(); REQUIRE(AdsGotoTop(hTable) == openads::AE_SUCCESS);
      CHECK(read_field1("GoTop") == 1u); std::printf("[1GB]   GoTop: %.1f ms\n", ms_since(t0)); }

    { auto t0 = clk::now(); REQUIRE(AdsGotoBottom(hTable) == openads::AE_SUCCESS);
      CHECK(read_field1("GoBottom") == cnt); std::printf("[1GB]   GoBottom: %.1f ms\n", ms_since(t0)); }

    { UNSIGNED32 mid = cnt / 2;
      auto t0 = clk::now(); REQUIRE(AdsGotoRecord(hTable, mid) == openads::AE_SUCCESS);
      CHECK(read_field1("GotoRecord(mid)") == mid); std::printf("[1GB]   GotoRecord: %.1f ms\n", ms_since(t0)); }

    { auto t0 = clk::now();
      for (int i = 0; i < 100; ++i) REQUIRE(AdsSkip(hTable, 1) == openads::AE_SUCCESS);
      UNSIGNED32 rec = 0; REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == openads::AE_SUCCESS);
      CHECK(rec == cnt / 2 + 100u);
      std::printf("[1GB] 100x Skip(+1): %.1f ms (%.2f ms/op)  -> recno=%u\n",
                  ms_since(t0), ms_since(t0) / 100.0, rec); }

    { UNSIGNED8 fc[8] = "CITY";
      UNSIGNED8 buf[64] = {0}; UNSIGNED32 cap = sizeof(buf);
      REQUIRE(AdsGetField(hTable, fc, buf, &cap, 0) == openads::AE_SUCCESS);
      std::printf("[1GB] CITY at recno %u = [%.*s]\n", cnt / 2 + 100u,
                  static_cast<int>(cap), reinterpret_cast<char*>(buf)); }

    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
}
