#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_driver.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::cdx::CdxDriver;
using openads::drivers::DriverOpenMode;

// Regression — ADSCDX error 5000 "record number out of range" during a
// multiuser REPLACE ... FOR (DBEVAL).
//
// A CdxDriver caches the DBF record count at open() time. When a peer
// connection appends a record afterwards (the normal multiuser ERP case:
// one station runs a batch REPLACE while another inserts), the first
// connection's cached count lags the on-disk truth. An index walk then
// hands read_record_raw()/write_record_raw() a recno that is valid on
// disk but beyond the stale cache, and the driver used to fail it hard
// with 5000 mid-scan — the exact symptom a user hit on CONSEINV.
//
// Native ADSCDX tolerates this (it re-reads the header). The fetch path
// now re-reads the on-disk count (under a shared header lock) before
// declaring a recno out of range, so a peer's freshly appended rows
// become reachable instead of crashing the scan. The slow path only runs
// when recno > cached count, so a normal forward scan pays nothing.
TEST_CASE("CdxDriver fetch sees a peer's appended record (no spurious 5000)") {
    auto dir = fs::temp_directory_path() / "openads_peer_append_5000";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    // Create "inv" with 3 records via the ABI, then close so the .dbf is a
    // plain file on disk that both drivers can open.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 def[]   = "ID,N,6,0";
        UNSIGNED8 tname[] = "inv";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(AdsAppendRecord(hT) == 0);
        }
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    const auto dbf = (dir / "inv.dbf").string();

    // Connection A opens and caches rec_count_ == 3.
    CdxDriver a;
    REQUIRE(a.open(dbf, DriverOpenMode::Shared).has_value());
    REQUIRE(a.record_count() == 3u);

    // Connection B (a peer) appends a 4th record on disk.
    {
        CdxDriver b;
        REQUIRE(b.open(dbf, DriverOpenMode::Shared).has_value());
        std::vector<std::uint8_t> rec(b.record_length(), 0x20); // spaces
        rec[0] = 0x20;                                           // not deleted
        auto ap = b.append_record_raw(rec.data(), rec.size());
        REQUIRE(ap.has_value());
        CHECK(ap.value() == 4u);
    }

    // A's cache still says 3, but recno 4 physically exists. Before the
    // fix read_record_raw(4) returns 5000; after it refreshes and reads.
    auto got = a.read_record_raw(4u);
    CHECK_MESSAGE(got.has_value(),
        "peer-appended recno 4 must be readable, not ADSCDX 5000");

    // The write path a REPLACE takes must also reach the row.
    if (got.has_value()) {
        auto wr = a.write_record_raw(4u, got.value().data(),
                                     got.value().size());
        CHECK_MESSAGE(wr.has_value(),
            "writing peer-appended recno 4 must not raise ADSCDX 5000");
    }

    fs::remove_all(dir, ec);
}
