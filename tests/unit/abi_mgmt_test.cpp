#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>

extern "C" {

// M9.24 Mg* surface — declarations match what rddads' adsmgmnt.c
// reaches for (signatures inferred from the Harbour call sites).
UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser,
                        UNSIGNED8* pucPwd, ADSHANDLE* phMgmt);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt);
UNSIGNED32 AdsMgGetServerType(ADSHANDLE hMgmt, UNSIGNED16* pusType);
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE hMgmt, void* pStruct,
                               UNSIGNED16* pusSize);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE hMgmt, void* pStruct,
                                UNSIGNED16* pusSize);
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE hMgmt, void* pBuf,
                             UNSIGNED16* pusCount);
UNSIGNED32 AdsMgKillUser(ADSHANDLE hMgmt, UNSIGNED8* pucUser,
                         UNSIGNED16 usOption);
UNSIGNED32 AdsMgResetCommStats(ADSHANDLE hMgmt);

}  // extern "C"

TEST_CASE("M9.24 AdsMgConnect produces a synthetic mgmt handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[8] = "u";
    UNSIGNED8 pwd[8] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    CHECK(h != 0);
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.24 AdsMgGetServerType reports unknown (0) without trapping") {
    UNSIGNED16 t = 99;
    REQUIRE(AdsMgGetServerType(/*hMgmt=*/1, &t) == 0);
    CHECK(t == 0);
}

TEST_CASE("M9.24 AdsMgGetInstallInfo zero-fills caller's struct") {
    // Simulate a 64-byte struct pre-poisoned with 0xAB. After the call
    // every byte must be zeroed so the caller doesn't read uninit data.
    std::array<std::uint8_t, 64> buf;
    buf.fill(0xAB);
    UNSIGNED16 sz = static_cast<UNSIGNED16>(buf.size());
    REQUIRE(AdsMgGetInstallInfo(/*hMgmt=*/1, buf.data(), &sz) == 0);
    for (auto b : buf) CHECK(b == 0);
}

TEST_CASE("M9.24 AdsMgGetActivityInfo zero-fills caller's struct") {
    std::array<std::uint8_t, 32> buf;
    buf.fill(0xFF);
    UNSIGNED16 sz = static_cast<UNSIGNED16>(buf.size());
    REQUIRE(AdsMgGetActivityInfo(/*hMgmt=*/1, buf.data(), &sz) == 0);
    for (auto b : buf) CHECK(b == 0);
}

TEST_CASE("M9.24 AdsMgGetUserNames reports empty list") {
    UNSIGNED8 buf[64] = {0xCC};
    UNSIGNED16 cnt    = 5;
    REQUIRE(AdsMgGetUserNames(/*hMgmt=*/1, buf, &cnt) == 0);
    CHECK(cnt == 0);
}

TEST_CASE("M9.24 AdsMgKillUser + AdsMgResetCommStats no-op success") {
    UNSIGNED8 user[8] = "alice";
    REQUIRE(AdsMgKillUser(/*hMgmt=*/1, user, /*usOption=*/0) == 0);
    REQUIRE(AdsMgResetCommStats(/*hMgmt=*/1) == 0);
}
