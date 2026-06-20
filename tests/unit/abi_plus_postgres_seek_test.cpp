#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>
#endif

#if defined(OPENADS_WITH_POSTGRESQL)

namespace {

void seed_fixture(PGconn* conn) {
    auto exec = [&](const char* sql) {
        PGresult* res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            FAIL("seed failed");
        }
        PQclear(res);
    };
    exec("DROP TABLE IF EXISTS clientes");
    exec("CREATE TABLE clientes (id INTEGER PRIMARY KEY, nome TEXT)");
    exec("INSERT INTO clientes (id, nome) VALUES (1,'Ana'),(2,'Bob'),(3,'Cid')");
}

} // namespace

TEST_CASE("ABI: postgresql AdsSeek on column index") {
    const char* uri_env = std::getenv("OPENADS_TEST_PG_URI");
    if (uri_env == nullptr || uri_env[0] == '\0') {
        SKIP("Set OPENADS_TEST_PG_URI for E2E test.");
    }

    PGconn* seed = PQconnectdb(uri_env);
    if (PQstatus(seed) != CONNECTION_OK) {
        PQfinish(seed);
        SKIP("Cannot connect with OPENADS_TEST_PG_URI.");
    }
    seed_fixture(seed);
    PQfinish(seed);

    const std::string uri = uri_env;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl[32] = "clientes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, tbl, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hTable) == 0);

    UNSIGNED8 tag[16] = "id";
    ADSHANDLE ahIndex[1] = {0};
    UNSIGNED16 nIdx = 0;
    REQUIRE(AdsOpenIndex(hTable, tag, ahIndex, &nIdx) == 0);
    REQUIRE(nIdx == 1);

    UNSIGNED8 key[] = "2";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(ahIndex[0], key, 1, 0, 0, &found) == 0);
    CHECK(found == 1);

    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"nome", buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), cap) ==
          std::string(64, ' ').replace(0, 3, "Bob"));

    REQUIRE(AdsCloseIndex(ahIndex[0]) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#endif