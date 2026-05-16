#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

// M9.25 — AdsMg* now report real telemetry (backed by MgCollector).
// A "local" AdsMgConnect yields an in-process backend that reports
// 1 connection / 1 user; unknown handles are rejected.
extern "C" {
UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser,
                        UNSIGNED8* pucPwd, ADSHANDLE* phMgmt);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt);
UNSIGNED32 AdsMgGetServerType(ADSHANDLE hMgmt, UNSIGNED16* pusType);
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE hMgmt, void* pStruct,
                               UNSIGNED16* pusSize);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE hMgmt, void* pStruct,
                                UNSIGNED16* pusSize);
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE hMgmt, UNSIGNED8* pucFile,
                             void* pBuf, UNSIGNED16* pusCount,
                             UNSIGNED16* pusSize);
UNSIGNED32 AdsMgKillUser(ADSHANDLE hMgmt, UNSIGNED8* pucUser,
                         UNSIGNED16 usConnNo);
UNSIGNED32 AdsMgResetCommStats(ADSHANDLE hMgmt);
}  // extern "C"

TEST_CASE("M9.25 AdsMgConnect (local) produces a real mgmt handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    CHECK(h != 0);
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetServerType reports 0 (local) for a local handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    UNSIGNED16 t = 99;
    REQUIRE(AdsMgGetServerType(h, &t) == 0);
    CHECK(t == 0);
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetInstallInfo reports the OpenADS version") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 sz = sizeof(info);
    REQUIRE(AdsMgGetInstallInfo(h, &info, &sz) == 0);
    CHECK(sz == sizeof(ADS_MGMT_INSTALL_INFO));
    std::string ver(reinterpret_cast<const char*>(info.aucVersionStr));
    CHECK(ver.rfind("OpenADS", 0) == 0);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetActivityInfo (local) reports 1 connection") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    REQUIRE(AdsMgGetActivityInfo(h, &act, &sz) == 0);
    CHECK(act.stConnections.ulInUse == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetUserNames (local) lists the in-process user") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    std::array<ADS_MGMT_USER_INFO, 4> buf;
    UNSIGNED16 cnt = static_cast<UNSIGNED16>(buf.size());
    UNSIGNED16 sz  = sizeof(ADS_MGMT_USER_INFO);
    REQUIRE(AdsMgGetUserNames(h, nullptr, buf.data(), &cnt, &sz) == 0);
    CHECK(cnt == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgKillUser + AdsMgResetCommStats succeed on a local handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    UNSIGNED8 user[8] = "alice";
    CHECK(AdsMgKillUser(h, user, /*usConnNo=*/0) == 0);
    CHECK(AdsMgResetCommStats(h) == 0);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMg* reject an unknown handle") {
    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    CHECK(AdsMgGetActivityInfo(/*bogus*/ 0x1234, &act, &sz) != 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 isz = sizeof(info);
    CHECK(AdsMgGetInstallInfo(0x1234, &info, &isz) != 0);
}
