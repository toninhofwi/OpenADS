#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// F4 — rolling back a transaction that APPENDED records must leave no
// trace of them: RECCOUNT goes back down, just like ADS. The engine
// used to soft-delete the appended row on rollback (set the deleted
// flag, leave record_count untouched), so AdsGetRecordCount — which
// reports the physical header count including deleted rows — still
// counted the reverted append.
TEST_CASE("Rollback of an appended record removes it physically") {
    auto dir = fs::temp_directory_path() / "openads_tx_rb_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,6,0;NAME,C,10,0";
    UNSIGNED8 tname[] = "jrnl";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 fid[] = "ID";

    // One committed baseline record so we can tell "rolled back to
    // baseline" from "table truncated to empty".
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetDouble(hT, fid, 1.0) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED32 base = 99;
    REQUIRE(AdsGetRecordCount(hT, 0, &base) == 0);
    CHECK(base == 1);

    // Append inside a transaction, then roll back.
    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetDouble(hT, fid, 2.0) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED32 during = 99;
    REQUIRE(AdsGetRecordCount(hT, 0, &during) == 0);
    CHECK(during == 2);

    REQUIRE(AdsRollbackTransaction(hConn) == 0);

    UNSIGNED32 after = 99;
    REQUIRE(AdsGetRecordCount(hT, 0, &after) == 0);
    INFO("record count after rollback = " << after);
    CHECK(after == 1);   // the appended row is gone, baseline survives

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// Same invariant for ROLLBACK TO SAVEPOINT: an append made after the
// savepoint must be physically gone after rolling back to it.
TEST_CASE("Rollback to savepoint removes an append made after it") {
    auto dir = fs::temp_directory_path() / "openads_sp_rb_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,6,0;NAME,C,10,0";
    UNSIGNED8 tname[] = "jrnl";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 fid[] = "ID";

    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetDouble(hT, fid, 1.0) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    UNSIGNED8 sp_name[16] = "sp1";
    REQUIRE(AdsCreateSavepoint(hConn, sp_name, ADS_DEFAULT) == 0);

    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetDouble(hT, fid, 2.0) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED32 during = 99;
    REQUIRE(AdsGetRecordCount(hT, 0, &during) == 0);
    CHECK(during == 2);

    REQUIRE(AdsRollbackTransaction80(hConn, sp_name, ADS_DEFAULT) == 0);

    UNSIGNED32 after = 99;
    REQUIRE(AdsGetRecordCount(hT, 0, &after) == 0);
    INFO("record count after rollback-to-savepoint = " << after);
    CHECK(after == 1);

    REQUIRE(AdsCommitTransaction(hConn) == 0);
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
