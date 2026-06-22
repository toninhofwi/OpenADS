// M4 ADT creation test.
// Verifies AdsCreateTable(ADS_ADT) produces a readable .adt file with the
// correct schema, and that records appended via the ABI round-trip correctly.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("M4 ADT create: AdsCreateTable(ADS_ADT) + append + reopen") {
    fs::path tmp = fs::temp_directory_path() / "openads_adt_create_test";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "people.adt", ec);
      fs::remove(tmp / "people.adm", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Field def: Name(CHAR 30) | Age(Numeric 5, maps to INTEGER) | Active(LOGICAL)
    UNSIGNED8 tbl[]    = "people.adt";
    UNSIGNED8 flddef[] = "Name,Character,30;Age,Numeric,5;Active,Logical";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);
    CHECK(hTable != 0);

    // Schema: 3 fields
    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == AE_SUCCESS);
    CHECK(nflds == 3);

    // Field 1 must be "Name" of type ADS_STRING
    UNSIGNED8  fname[64]{};
    UNSIGNED16 fnlen = sizeof(fname);
    REQUIRE(AdsGetFieldName(hTable, 1, fname, &fnlen) == AE_SUCCESS);
    CHECK(std::string(reinterpret_cast<char*>(fname)) == "Name");

    UNSIGNED16 ftype = 0;
    REQUIRE(AdsGetFieldType(hTable, fname, &ftype) == AE_SUCCESS);
    CHECK(ftype == ADS_STRING);

    // Append record 1: Name="Alice", Age=30, Active=true
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    {
        UNSIGNED8 name_f[]   = "Name";
        UNSIGNED8 name_val[] = "Alice";
        REQUIRE(AdsSetString(hTable, name_f, name_val,
                             static_cast<UNSIGNED32>(std::strlen("Alice")))
                == AE_SUCCESS);

        UNSIGNED8 age_f[] = "Age";
        REQUIRE(AdsSetDouble(hTable, age_f, 30.0) == AE_SUCCESS);

        UNSIGNED8 act_f[] = "Active";
        REQUIRE(AdsSetLogical(hTable, act_f, 1) == AE_SUCCESS);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    // Append record 2: Name="Bob", Age=25, Active=false
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    {
        UNSIGNED8 name_f[]   = "Name";
        UNSIGNED8 name_val[] = "Bob";
        REQUIRE(AdsSetString(hTable, name_f, name_val,
                             static_cast<UNSIGNED32>(std::strlen("Bob")))
                == AE_SUCCESS);

        UNSIGNED8 age_f[] = "Age";
        REQUIRE(AdsSetDouble(hTable, age_f, 25.0) == AE_SUCCESS);

        UNSIGNED8 act_f[] = "Active";
        REQUIRE(AdsSetLogical(hTable, act_f, 0) == AE_SUCCESS);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // ── Reopen and verify ────────────────────────────────────────────────────
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT,
                         &hTable) == AE_SUCCESS);

    UNSIGNED32 nrecs = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 2);

    // Record 1: Name="Alice", Age=30, Active=true
    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    {
        UNSIGNED8 name_f[]  = "Name";
        UNSIGNED8 name_buf[64]{};
        UNSIGNED32 name_len = sizeof(name_buf);
        REQUIRE(AdsGetString(hTable, name_f, name_buf, &name_len, 0)
                == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(name_buf), name_len) == "Alice");

        UNSIGNED8 age_f[]  = "Age";
        SIGNED32  age_val  = 0;
        REQUIRE(AdsGetLong(hTable, age_f, &age_val) == AE_SUCCESS);
        CHECK(age_val == 30);

        UNSIGNED8  act_f[]  = "Active";
        UNSIGNED16 act_val  = 0;
        REQUIRE(AdsGetLogical(hTable, act_f, &act_val) == AE_SUCCESS);
        CHECK(act_val != 0);
    }

    // Record 2: Name="Bob", Age=25, Active=false
    REQUIRE(AdsGotoRecord(hTable, 2) == AE_SUCCESS);
    {
        UNSIGNED8 name_f[]  = "Name";
        UNSIGNED8 name_buf[64]{};
        UNSIGNED32 name_len = sizeof(name_buf);
        REQUIRE(AdsGetString(hTable, name_f, name_buf, &name_len, 0)
                == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(name_buf), name_len) == "Bob");

        UNSIGNED8 age_f[]  = "Age";
        SIGNED32  age_val  = 0;
        REQUIRE(AdsGetLong(hTable, age_f, &age_val) == AE_SUCCESS);
        CHECK(age_val == 25);

        UNSIGNED8  act_f[]  = "Active";
        UNSIGNED16 act_val  = 0;
        REQUIRE(AdsGetLogical(hTable, act_f, &act_val) == AE_SUCCESS);
        CHECK(act_val == 0);
    }

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
}

// Regression: field-name resolution through the ABI must be case-insensitive
// (native ACE semantics). resolve_field_index used an exact-case compare, so
// AdsGetString/AdsSetString with a case differing from the stored field name
// — and, crucially, CDX/NTX index expressions stored in a different case than
// the (upper-cased) DBF field names — spuriously failed with COLUMN_NOT_FOUND.
TEST_CASE("ABI field-name resolution is case-insensitive") {
    fs::path tmp = fs::temp_directory_path() / "openads_field_ci_test";
    { std::error_code ec; fs::create_directories(tmp, ec);
      fs::remove(tmp / "ci.adt", ec); fs::remove(tmp / "ci.adm", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Stored field name is mixed-case "MixedName".
    UNSIGNED8 tbl[]    = "ci.adt";
    UNSIGNED8 flddef[] = "MixedName,Character,20";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hT) == AE_SUCCESS);

    // Write through a DIFFERENT case than stored.
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    UNSIGNED8 wf[] = "MIXEDNAME";
    UNSIGNED8 wv[] = "hello";
    REQUIRE(AdsSetString(hT, wf, wv,
                         static_cast<UNSIGNED32>(std::strlen("hello")))
            == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);

    // Read through several cases — all must resolve to the same field.
    const char* variants[] = {"mixedname", "MIXEDNAME", "MixedName", "mIxEdNaMe"};
    for (const char* v : variants) {
        UNSIGNED8 rf[32]{};
        std::memcpy(rf, v, std::strlen(v));
        UNSIGNED8  rb[64]{};
        UNSIGNED32 rl = sizeof(rb);
        REQUIRE(AdsGetString(hT, rf, rb, &rl, 0) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(rb), rl) == "hello");
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
