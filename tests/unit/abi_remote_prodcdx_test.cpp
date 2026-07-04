// abi_remote_prodcdx_test.cpp — Tests REMOTE mode against a pre-existing
// production database (DBF/CDX) exactly as FWH rddads does it:
//   AdsConnect60(tcp://) → AdsOpenTable → auto-open production CDX
//   → FieldGet → AdsSetIndexOrder → OrdBagName → ordered navigation
//
// This validates the 3 issues reported by the FWH user:
//   1. FieldGet after open (needs GoTop first in remote mode)
//   2. OrdBagName returns bag path after auto-open
//   3. AdsSetIndexOrder by name/handle changes cursor order
//
// Run with:
//   $env:OPENADS_TEST_REMOTE = "tcp://192.168.18.184:16262/"
//   openads_unit_tests -tc="REMOTE*"
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Configuration: set OPENADS_TEST_REMOTE to e.g.
// "tcp://192.168.18.184:16262/" to run against a live remote server.
// Without it, every test is skipped.
// ---------------------------------------------------------------------------
namespace {
const char* remote_uri_env() {
    return std::getenv("OPENADS_TEST_REMOTE");
}
}  // namespace

// ---------------------------------------------------------------------------
// Helper: connect to remote server and open a table
// ---------------------------------------------------------------------------
struct RemoteFixture {
    ADSHANDLE hConn  = 0;
    ADSHANDLE hTable = 0;
    bool connected = false;

    bool connect(const char* uri) {
        std::string u = uri;
        std::vector<UNSIGNED8> buf(u.begin(), u.end());
        buf.push_back(0);
        UNSIGNED32 rc = AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                                     nullptr, nullptr, 0, &hConn);
        connected = (rc == 0 && hConn != 0);
        return connected;
    }

    bool open_table(const char* name) {
        if (!connected) return false;
        UNSIGNED32 rc = AdsOpenTable(hConn, (UNSIGNED8*)name,
                                      (UNSIGNED8*)"T",
                                      ADS_CDX, 0, 0, 0, 0, &hTable);
        return rc == 0;
    }

    void close() {
        if (hTable) { AdsCloseTable(hTable); hTable = 0; }
        if (hConn)  { AdsDisconnect(hConn);   hConn = 0; }
        connected = false;
    }

    ~RemoteFixture() { close(); }
};

// ===========================================================================
// TEST 1: AdsOpenTable opens production CDX automatically
//         OrdBagName (AdsGetIndexFilename) returns the bag path
// ===========================================================================
TEST_CASE("REMOTE: OrdBagName returns production CDX bag path after open"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    // After AdsOpenTable with ADS_CDX, the production CDX should be
    // auto-associated. AdsGetNumIndexes should return > 0.
    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(f.hTable, &nidx) == 0);
    CHECK(nidx > 0);
    std::printf("  AdsGetNumIndexes = %u\n", nidx);

    // Get the first index handle (order 1 = first tag in production CDX)
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 1, &hIdx) == 0);

    // OrdBagName: AdsGetIndexFilename should return the CDX filename
    UNSIGNED8 bag[256] = {0};
    UNSIGNED16 baglen = sizeof(bag);
    UNSIGNED32 rc = AdsGetIndexFilename(hIdx, 0, bag, &baglen);
    CHECK(rc == 0);
    std::string bagname(reinterpret_cast<char*>(bag), baglen);
    CHECK(!bagname.empty());
    std::printf("  OrdBagName = '%s'\n", bagname.c_str());

    // Should contain "customer" and ".cdx" (case-insensitive)
    std::string baglower = bagname;
    for (auto& c : baglower) c = (char)std::tolower((unsigned char)c);
    CHECK(baglower.find("customer") != std::string::npos);
    CHECK(baglower.find(".cdx") != std::string::npos);

    f.close();
}

// ===========================================================================
// TEST 2: AdsGetIndexName returns the tag name for each order
// ===========================================================================
TEST_CASE("REMOTE: AdsGetIndexName returns tag names for all orders"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(f.hTable, &nidx) == 0);
    REQUIRE(nidx > 0);

    for (UNSIGNED16 ord = 1; ord <= nidx; ++ord) {
        ADSHANDLE hIdx = 0;
        UNSIGNED32 rc = AdsGetIndexHandleByOrder(f.hTable, ord, &hIdx);
        REQUIRE(rc == 0);

        UNSIGNED8 name[256] = {0};
        UNSIGNED16 namelen = sizeof(name);
        rc = AdsGetIndexName(hIdx, name, &namelen);
        CHECK(rc == 0);
        std::string tagname(reinterpret_cast<char*>(name), namelen);
        CHECK(!tagname.empty());
        std::printf("  Order %u: tag='%s' (len=%u)\n", ord,
                    tagname.c_str(), static_cast<unsigned>(namelen));
    }

    // customer.cdx on the iMac test dataset: CUSTNO + CUSTNAME (expr NAME).
    {
        ADSHANDLE h1 = 0, h2 = 0;
        REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 1, &h1) == 0);
        REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 2, &h2) == 0);
        UNSIGNED8 n1[32] = {0}, n2[32] = {0};
        UNSIGNED16 l1 = sizeof(n1), l2 = sizeof(n2);
        REQUIRE(AdsGetIndexName(h1, n1, &l1) == 0);
        REQUIRE(AdsGetIndexName(h2, n2, &l2) == 0);
        std::string t1(reinterpret_cast<char*>(n1), l1);
        std::string t2(reinterpret_cast<char*>(n2), l2);
        CHECK(t1 == "CUSTNO");
        CHECK(t2 == "CUSTNAME");
    }

    f.close();
}

// ===========================================================================
// TEST 3: GoTop + FieldGet on first record (the crash scenario)
// ===========================================================================
TEST_CASE("REMOTE: GoTop + FieldGet on first record after open"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    // ADS semantics: after open, record buffer is NOT populated in
    // remote mode. GoTop is required before any FieldGet.
    REQUIRE(AdsGotoTop(f.hTable) == 0);

    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(f.hTable, 0, &recno) == 0);
    CHECK(recno >= 1u);
    std::printf("  After GoTop: recno=%u\n", recno);

    // FieldGet by ordinal — ACE idiom: cast the 1-based ordinal to a
    // pointer, NOT a string "1". AdsGetField detects small pointer
    // values as ordinals.
    UNSIGNED8 val[512] = {0};
    UNSIGNED32 vallen = sizeof(val) - 1;
    UNSIGNED32 rc = AdsGetField(f.hTable,
        reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1)),
        val, &vallen, 0);
    CHECK(rc == 0);
    std::string fieldval(reinterpret_cast<char*>(val), vallen);
    CHECK(!fieldval.empty());
    std::printf("  Field(1) = '%s'\n", fieldval.c_str());

    // Also try by name — get the first field name
    UNSIGNED8 fname[64] = {0};
    UNSIGNED16 fnamelen = sizeof(fname);
    rc = AdsGetFieldName(f.hTable, 1, fname, &fnamelen);
    if (rc == 0) {
        std::string fn(reinterpret_cast<char*>(fname), fnamelen);
        UNSIGNED8 fval2[512] = {0};
        UNSIGNED32 fval2len = sizeof(fval2) - 1;
        rc = AdsGetField(f.hTable, fname, fval2, &fval2len, 0);
        CHECK(rc == 0);
        std::string fv(reinterpret_cast<char*>(fval2), fval2len);
        std::printf("  Field '%s' = '%s'\n", fn.c_str(), fv.c_str());
    }

    f.close();
}

// ===========================================================================
// TEST 4: AdsSetIndexOrderByHandle changes cursor order
// ===========================================================================
TEST_CASE("REMOTE: AdsSetIndexOrderByHandle changes cursor order"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(f.hTable, &nidx) == 0);
    REQUIRE(nidx >= 1);

    // Natural order: GoTop -> first record
    REQUIRE(AdsGotoTop(f.hTable) == 0);
    UNSIGNED32 rec_natural = 0;
    REQUIRE(AdsGetRecordNum(f.hTable, 0, &rec_natural) == 0);
    std::printf("  Natural order top: recno=%u\n", rec_natural);

    // Set order to tag handle 1
    ADSHANDLE hIdx1 = 0;
    REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 1, &hIdx1) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(f.hTable, hIdx1) == 0);
    REQUIRE(AdsGotoTop(f.hTable) == 0);
    UNSIGNED32 rec_ord1 = 0;
    REQUIRE(AdsGetRecordNum(f.hTable, 0, &rec_ord1) == 0);
    std::printf("  Order 1 top: recno=%u\n", rec_ord1);

    // If there's a second tag, compare ordering
    if (nidx >= 2) {
        ADSHANDLE hIdx2 = 0;
        REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 2, &hIdx2) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(f.hTable, hIdx2) == 0);
        REQUIRE(AdsGotoTop(f.hTable) == 0);
        UNSIGNED32 rec_ord2 = 0;
        REQUIRE(AdsGetRecordNum(f.hTable, 0, &rec_ord2) == 0);
        std::printf("  Order 2 top: recno=%u\n", rec_ord2);
        bool differ = (rec_ord1 != rec_natural) || (rec_ord2 != rec_natural);
        CHECK(differ);
    } else {
        // Single tag: AdsSetIndexOrderByHandle should succeed without error.
        // The handle should resolve and GoTop should land on a valid record.
        CHECK(rec_ord1 >= 1u);
    }

    f.close();
}

// ===========================================================================
// TEST 5: AdsSetIndexOrder by tag name
// ===========================================================================
TEST_CASE("REMOTE: AdsSetIndexOrder by tag name"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(f.hTable, &nidx) == 0);
    REQUIRE(nidx > 0);

    // Get first tag name
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 1, &hIdx) == 0);
    UNSIGNED8 tname[256] = {0};
    UNSIGNED16 tnamelen = sizeof(tname);
    REQUIRE(AdsGetIndexName(hIdx, tname, &tnamelen) == 0);
    std::string tag1(reinterpret_cast<char*>(tname), tnamelen);
    std::printf("  Tag 1 name = '%s'\n", tag1.c_str());

    // Set order by name
    std::vector<UNSIGNED8> tnamebuf(tag1.begin(), tag1.end());
    tnamebuf.push_back(0);
    REQUIRE(AdsSetIndexOrder(f.hTable, tnamebuf.data()) == 0);
    REQUIRE(AdsGotoTop(f.hTable) == 0);
    UNSIGNED32 rec1 = 0;
    REQUIRE(AdsGetRecordNum(f.hTable, 0, &rec1) == 0);
    std::printf("  After AdsSetIndexOrder GoTop: recno=%u\n", rec1);

    // Verify we can walk a few records
    for (int i = 0; i < 5; ++i) {
        UNSIGNED16 is_eof = 0;
        AdsAtEOF(f.hTable, &is_eof);
        if (is_eof) break;
        UNSIGNED32 r = 0;
        AdsGetRecordNum(f.hTable, 0, &r);
        UNSIGNED8 val[256] = {0};
        UNSIGNED32 vlen = sizeof(val) - 1;
        AdsGetField(f.hTable,
            reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1)),
            val, &vlen, 0);
        std::string v(reinterpret_cast<char*>(val), vlen);
        std::printf("    recno=%u field1='%s'\n", r, v.c_str());
        AdsSkip(f.hTable, 1);
    }

    f.close();
}

// ===========================================================================
// TEST 6: Walk entire table via ordered scan
// ===========================================================================
TEST_CASE("REMOTE: ordered full-scan counts match record count"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    UNSIGNED32 total = 0;
    REQUIRE(AdsGetRecordCount(f.hTable, 0, &total) == 0);
    CHECK(total > 0u);
    std::printf("  Record count = %u\n", total);

    // Set order to first tag
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandleByOrder(f.hTable, 1, &hIdx) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(f.hTable, hIdx) == 0);
    REQUIRE(AdsGotoTop(f.hTable) == 0);

    UNSIGNED32 count = 0;
    for (;;) {
        UNSIGNED16 is_eof = 0;
        AdsAtEOF(f.hTable, &is_eof);
        if (is_eof) break;
        ++count;
        AdsSkip(f.hTable, 1);
    }
    std::printf("  Ordered scan: %u records\n", count);
    CHECK(count == total);

    f.close();
}

// ===========================================================================
// TEST 6b: workorders in subdirectory (iMac /tmp/openads_mac/orders/)
// ===========================================================================
TEST_CASE("REMOTE: workorders subdirectory auto-binds production CDX"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(f.hConn, (UNSIGNED8*)"orders/workorders.dbf",
                          (UNSIGNED8*)"WO",
                          ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx > 0);
    std::printf("  orders/workorders.dbf: AdsGetNumIndexes = %u\n", nidx);

    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hIdx) == 0);
    UNSIGNED8 bag[256] = {0};
    UNSIGNED16 baglen = sizeof(bag);
    REQUIRE(AdsGetIndexFilename(hIdx, 0, bag, &baglen) == 0);
    std::string bagname(reinterpret_cast<char*>(bag), baglen);
    CHECK(!bagname.empty());
    std::printf("  OrdBagName = '%s'\n", bagname.c_str());

    UNSIGNED32 recs = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &recs) == 0);
    std::printf("  Record count = %u\n", recs);
    CHECK(recs > 0u);

    AdsCloseTable(hTable);
    f.close();
}

// ===========================================================================
// TEST 7: Multiple tables open simultaneously
// ===========================================================================
TEST_CASE("REMOTE: multiple tables open simultaneously"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    ADSHANDLE hInv = 0;
    REQUIRE(AdsOpenTable(f.hConn, (UNSIGNED8*)"invoices.dbf",
                          (UNSIGNED8*)"INV",
                          ADS_CDX, 0, 0, 0, 0, &hInv) == 0);

    UNSIGNED32 cust_cnt = 0, inv_cnt = 0;
    REQUIRE(AdsGetRecordCount(f.hTable, 0, &cust_cnt) == 0);
    REQUIRE(AdsGetRecordCount(hInv, 0, &inv_cnt) == 0);
    std::printf("  customer.dbf: %u records\n", cust_cnt);
    std::printf("  invoices.dbf: %u records\n", inv_cnt);
    CHECK(cust_cnt > 0u);
    CHECK(inv_cnt > 0u);

    // Both should have production CDX
    UNSIGNED16 ci = 0, ii = 0;
    REQUIRE(AdsGetNumIndexes(f.hTable, &ci) == 0);
    REQUIRE(AdsGetNumIndexes(hInv, &ii) == 0);
    std::printf("  customer indexes: %u, invoices indexes: %u\n", ci, ii);
    CHECK(ci > 0);
    CHECK(ii > 0);

    AdsCloseTable(hInv);
    f.close();
}

// ===========================================================================
// TEST 8: FieldGet on multiple fields + record navigation
// ===========================================================================
TEST_CASE("REMOTE: FieldGet on multiple fields after Skip"
          * doctest::skip(remote_uri_env() == nullptr)) {
    RemoteFixture f;
    REQUIRE(f.connect(remote_uri_env()));
    REQUIRE(f.open_table("customer.dbf"));

    // Describe the table to know field names
    UNSIGNED16 nfields = 0;
    AdsGetNumFields(f.hTable, &nfields);
    std::printf("  Field count: %u\n", nfields);

    // GoTop and read first 5 records, all fields
    REQUIRE(AdsGotoTop(f.hTable) == 0);

    for (int row = 0; row < 5; ++row) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(f.hTable, &eof);
        if (eof) break;

        UNSIGNED32 recno = 0;
        AdsGetRecordNum(f.hTable, 0, &recno);

        std::printf("  --- Record %u (recno=%u) ---\n", row + 1, recno);
        for (UNSIGNED16 fi = 1; fi <= nfields; ++fi) {
            UNSIGNED8 fname[64] = {0};
            UNSIGNED16 fnlen = sizeof(fname);
            if (AdsGetFieldName(f.hTable, fi, fname, &fnlen) != 0) continue;

            UNSIGNED8 val[512] = {0};
            UNSIGNED32 vlen = sizeof(val) - 1;
            UNSIGNED32 rc = AdsGetField(f.hTable, fname, val, &vlen, 0);
            if (rc != 0) {
                std::printf("    %s: ERROR rc=%u\n", fname, rc);
                continue;
            }
            std::string v(reinterpret_cast<char*>(val), vlen);
            std::printf("    %s = '%s'\n", fname, v.c_str());
        }
        AdsSkip(f.hTable, 1);
    }

    f.close();
}
