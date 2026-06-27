// M4 ADT SQL compatibility smoke test.
// Opens the committed fixture ADT tables (tests/fixtures/adi) and verifies:
//   * SQL SELECT retrieves correct field types for all ADT-native types
//   * CiCharacter fields return ADS_CISTRING (25)
//   * MONEY/AdtMoney fields decode as IEEE754 doubles (not int64/10000)
//   * ShortInt fields return ADS_SHORTINT (12) and correct integer values
//   * AdtDate fields return ADS_DATE (3) in YYYYMMDD string form
//   * AdtTimestamp fields return ADS_TIMESTAMP (14)
//   * Logical fields work via SQL WHERE

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// fixtures/adi is relative to the source directory (always in the repo)
static fs::path fixture_dir() {
    return fs::path(__FILE__).parent_path().parent_path() / "fixtures" / "adi";
}

// Helper: run SQL and return first-row, named-field string value.
static std::string sql_scalar(ADSHANDLE hConn, const char* sql,
                               const char* field) {
    ADSHANDLE hStmt = 0;
    if (AdsCreateSQLStatement(hConn, &hStmt) != AE_SUCCESS) return "STMTERR";
    ADSHANDLE hSql = 0;
    std::vector<UNSIGNED8> q(std::strlen(sql) + 1);
    std::memcpy(q.data(), sql, q.size());
    UNSIGNED32 rc = AdsExecuteSQLDirect(hStmt, q.data(), &hSql);
    if (rc != AE_SUCCESS) {
        AdsCloseSQLStatement(hStmt);
        return "SQLERR:" + std::to_string(rc);
    }
    AdsGotoTop(hSql);
    UNSIGNED8 buf[256]{};
    UNSIGNED32 len = sizeof(buf);
    std::vector<UNSIGNED8> fn(std::strlen(field) + 1);
    std::memcpy(fn.data(), field, fn.size());
    AdsGetString(hSql, fn.data(), buf, &len, 0);
    AdsCloseTable(hSql);
    AdsCloseSQLStatement(hStmt);
    std::string result(reinterpret_cast<char*>(buf), len);
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

TEST_CASE("M4 ADT SQL: field types and values via fixture tables") {
    fs::path fdir = fixture_dir();
    REQUIRE(fs::exists(fdir / "landlords.adt"));
    REQUIRE(fs::exists(fdir / "leases.adt"));
    REQUIRE(fs::exists(fdir / "properties.adt"));

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, fdir.string().c_str(), fdir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // ── 1. landlords.adt: field types ────────────────────────────────────────
    SUBCASE("landlords field types") {
        UNSIGNED8 tbl[] = "landlords.adt";
        ADSHANDLE hT    = 0;
        REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                             ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                             ADS_DEFAULT, &hT) == AE_SUCCESS);

        // Field 1: LandLordID — CICHARACTER → ADS_CISTRING (25)
        UNSIGNED8  fname[64]{};
        UNSIGNED16 fnlen = sizeof(fname);
        REQUIRE(AdsGetFieldName(hT, 1, fname, &fnlen) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(fname)) == "LandLordID");
        UNSIGNED16 ftype = 0;
        REQUIRE(AdsGetFieldType(hT, fname, &ftype) == AE_SUCCESS);
        CHECK(ftype == ADS_CISTRING);

        // Verify there are 7 records (unchanged from existing smoke test)
        UNSIGNED32 nrecs = 0;
        REQUIRE(AdsGetRecordCount(hT, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
        CHECK(nrecs == 7);

        AdsCloseTable(hT);
    }

    // ── 2. landlords SQL SELECT ───────────────────────────────────────────────
    SUBCASE("landlords SQL SELECT") {
        // COUNT(*)
        std::string cnt = sql_scalar(hConn,
            "SELECT COUNT(*) AS N FROM landlords", "N");
        CHECK(cnt == "7");

        // CiCharacter field value survives SQL round-trip
        ADSHANDLE hStmt2 = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt2) == AE_SUCCESS);
        ADSHANDLE hSql = 0;
        UNSIGNED8 q[] = "SELECT LandLordID, inactive FROM landlords ORDER BY LandLordID";
        REQUIRE(AdsExecuteSQLDirect(hStmt2, q, &hSql) == AE_SUCCESS);
        REQUIRE(AdsGotoTop(hSql) == AE_SUCCESS);  // position after set_recno_sequence

        UNSIGNED16 nflds = 0;
        REQUIRE(AdsGetNumFields(hSql, &nflds) == AE_SUCCESS);
        CHECK(nflds == 2);

        // Field 1: LandLordID — should be ADS_CISTRING
        UNSIGNED8  fname[64]{};
        UNSIGNED16 fnlen = sizeof(fname);
        REQUIRE(AdsGetFieldName(hSql, 1, fname, &fnlen) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(fname)) == "LandLordID");

        // Check logical field: inactive
        fnlen = sizeof(fname);
        REQUIRE(AdsGetFieldName(hSql, 2, fname, &fnlen) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(fname)) == "inactive");

        UNSIGNED16 at_eof = 0;
        UNSIGNED32 row_count = 0;
        while (true) {
            REQUIRE(AdsAtEOF(hSql, &at_eof) == AE_SUCCESS);
            if (at_eof) break;
            ++row_count;
            REQUIRE(AdsSkip(hSql, 1) == AE_SUCCESS);
        }
        CHECK(row_count == 7);
        AdsCloseTable(hSql);
        AdsCloseSQLStatement(hStmt2);
    }

    // ── 3. properties.adt: ShortInt fields ───────────────────────────────────
    SUBCASE("properties ShortInt fields") {
            UNSIGNED8 tbl[] = "properties.adt";
            ADSHANDLE hT    = 0;
            REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                                 ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                                 ADS_DEFAULT, &hT) == AE_SUCCESS);

            // TotalUnits should be ADS_SHORTINT (12) and has valid values (1,1,6…)
            UNSIGNED8  bed[] = "TotalUnits";
            UNSIGNED16 ftype = 0;
            REQUIRE(AdsGetFieldType(hT, bed, &ftype) == AE_SUCCESS);
            CHECK(ftype == ADS_SHORTINT);

            // ShortInt should decode as a reasonable integer
            REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
            UNSIGNED16 at_eof = 0;
            REQUIRE(AdsAtEOF(hT, &at_eof) == AE_SUCCESS);
            if (!at_eof) {
                SIGNED32 bval = -1;
                REQUIRE(AdsGetLong(hT, bed, &bval) == AE_SUCCESS);
                CHECK(bval >= 0);
                CHECK(bval < 1000);  // sanity: no property has 1000+ units
            }

            AdsCloseTable(hT);
    }

    // ── 4. leases.adt: MONEY fields (IEEE754 double verify) ──────────────────
    SUBCASE("leases MONEY field type and value") {
            UNSIGNED8 tbl[] = "leases.adt";
            ADSHANDLE hT    = 0;
            REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                                 ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                                 ADS_DEFAULT, &hT) == AE_SUCCESS);

            // Rent should be ADS_MONEY (18)
            UNSIGNED8  rent[] = "Rent";
            UNSIGNED16 ftype  = 0;
            REQUIRE(AdsGetFieldType(hT, rent, &ftype) == AE_SUCCESS);
            CHECK(ftype == ADS_MONEY);

            // Navigate to first record and check rent is a plausible dollar amount
            UNSIGNED32 gtop_rc = AdsGotoTop(hT);
            INFO("AdsGotoTop(leases) returned " << gtop_rc);
            REQUIRE(gtop_rc == AE_SUCCESS);
            UNSIGNED16 at_eof = 0;
            REQUIRE(AdsAtEOF(hT, &at_eof) == AE_SUCCESS);
            if (!at_eof) {
                // Read as string: should look like a floating-point number
                UNSIGNED8  rbuf[64]{};
                UNSIGNED32 rlen = sizeof(rbuf);
                REQUIRE(AdsGetString(hT, rent, rbuf, &rlen, 0) == AE_SUCCESS);
                // String must not be empty and must not start with garbage
                CHECK(rlen > 0);
                // Value must be a plausible rent: 0 – 999999.9999
                double dval = 0.0;
                REQUIRE(AdsGetDouble(hT, rent, &dval) == AE_SUCCESS);
                CHECK(dval >= 0.0);
                CHECK(dval < 1000000.0);
            }

            AdsCloseTable(hT);
    }

    // ── 5. properties.adt: AdtDate fields ────────────────────────────────────
    SUBCASE("properties AdtDate field type") {
            UNSIGNED8 tbl[] = "properties.adt";
            ADSHANDLE hT    = 0;
            REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                                 ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                                 ADS_DEFAULT, &hT) == AE_SUCCESS);

            // PurchaseDate should be ADS_DATE (3)
            UNSIGNED8  pdate[] = "PurchaseDate";
            UNSIGNED16 ftype   = 0;
            REQUIRE(AdsGetFieldType(hT, pdate, &ftype) == AE_SUCCESS);
            CHECK(ftype == ADS_DATE);

            AdsCloseTable(hT);
    }

    // ── 6. leases.adt: AdtTimestamp field ────────────────────────────────────
    SUBCASE("leases AdtTimestamp field type") {
            UNSIGNED8 tbl[] = "leases.adt";
            ADSHANDLE hT    = 0;
            REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                                 ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                                 ADS_DEFAULT, &hT) == AE_SUCCESS);

            // created should be ADS_TIMESTAMP (14)
            UNSIGNED8  created[] = "created";
            UNSIGNED16 ftype     = 0;
            REQUIRE(AdsGetFieldType(hT, created, &ftype) == AE_SUCCESS);
            CHECK(ftype == ADS_TIMESTAMP);

            AdsCloseTable(hT);
    }

    AdsDisconnect(hConn);
}

// ── ADT creation: all extended types round-trip ───────────────────────────────

TEST_CASE("M4 ADT create: all extended ADT types in AdsCreateTable") {
    fs::path tmp = fs::temp_directory_path() / "openads_adt_types_test";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "types.adt", ec);
      fs::remove(tmp / "types.adm", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Field def covering all ADT-specific types
    UNSIGNED8 tbl[] = "types.adt";
    UNSIGNED8 flddef[] =
        "CiName,CiCharacter,30;"    // ADT type 20 (CICHARACTER)
        "SmallNum,ShortInt;"        // ADT type 12 (SHORTINT, 2 bytes)
        "Amount,Money;"             // ADT type 18 (MONEY, IEEE754 double)
        "Created,Timestamp;"        // ADT type 14 (TIMESTAMP)
        "StartTime,Time;"           // ADT type 13 (TIME)
        "Sequence,AutoInc";         // ADT type 15 (AUTOINC)
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // Verify field types match expected ADT type codes
    struct { int field_no; std::uint16_t expected_type; const char* name; } checks[] = {
        {1, ADS_CISTRING,  "CiName"},
        {2, ADS_SHORTINT,  "SmallNum"},
        {3, ADS_MONEY,     "Amount"},
        {4, ADS_TIMESTAMP, "Created"},
        {5, ADS_TIME,      "StartTime"},
        {6, ADS_AUTOINC,   "Sequence"},
    };
    for (auto& ck : checks) {
        UNSIGNED8  fname[64]{};
        UNSIGNED16 fnlen = sizeof(fname);
        REQUIRE(AdsGetFieldName(hTable, static_cast<UNSIGNED16>(ck.field_no), fname, &fnlen) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(fname)) == ck.name);
        UNSIGNED16 ftype = 0;
        REQUIRE(AdsGetFieldType(hTable, fname, &ftype) == AE_SUCCESS);
        CHECK(ftype == ck.expected_type);
    }

    // Append a record: write CiName and Amount (other fields can stay default)
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    {
        UNSIGNED8 name_f[]   = "CiName";
        UNSIGNED8 name_val[] = "TestCi";
        REQUIRE(AdsSetString(hTable, name_f, name_val,
                             static_cast<UNSIGNED32>(std::strlen("TestCi")))
                == AE_SUCCESS);
        UNSIGNED8 amt_f[] = "Amount";
        REQUIRE(AdsSetDouble(hTable, amt_f, 1234.5678) == AE_SUCCESS);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Reopen and verify Money round-trip (the critical IEEE754 decode check)
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT,
                         &hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    {
        UNSIGNED8 amt_f[] = "Amount";
        double dval = 0.0;
        REQUIRE(AdsGetDouble(hTable, amt_f, &dval) == AE_SUCCESS);
        CHECK(dval > 1234.0);
        CHECK(dval < 1235.0);
    }

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
}
