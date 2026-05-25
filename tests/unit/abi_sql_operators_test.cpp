// Tests for SQL operator completeness (IN, IS NULL, IS NOT NULL, BETWEEN)
// in aggregate FILTER compile path, plus AdsEvalLogicalExpr /
// AdsEvalNumericExpr / AdsEvalStringExpr.

#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_dbf(const fs::path& path,
               const std::vector<std::pair<std::string,
                   std::pair<char, std::uint8_t>>>& schema,
               const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(rows.size());
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * schema.size() + 1);
    std::uint16_t rec_len = 1;
    for (auto& s : schema) rec_len += s.second.second;
    hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    for (auto& s : schema) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()),
                     s.first.c_str(), 11);
        fd[11] = static_cast<std::uint8_t>(s.second.first);
        fd[16] = s.second.second;
        push(fd.data(), fd.size());
    }
    file.push_back(0x0D);
    for (auto& row : rows) {
        file.push_back(' ');
        for (std::size_t i = 0; i < schema.size(); ++i) {
            const auto& v = row[i];
            std::uint8_t L = schema[i].second.second;
            for (std::uint8_t k = 0; k < L; ++k)
                file.push_back(static_cast<std::uint8_t>(
                    k < v.size() ? v[k] : ' '));
        }
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

double run_agg_double(ADSHANDLE hConn, const char* sql, const char* col) {
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    std::vector<UNSIGNED8> q(std::strlen(sql) + 1);
    std::memcpy(q.data(), sql, q.size());
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, q.data(), &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8 buf[64] = {};
    UNSIGNED32 cap = static_cast<UNSIGNED32>(sizeof(buf) - 1);
    std::vector<UNSIGNED8> cn(std::strlen(col) + 1);
    std::memcpy(cn.data(), col, cn.size());
    REQUIRE(AdsGetField(hCur, cn.data(), buf, &cap, 0) == 0);
    AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    buf[cap] = '\0';
    return std::strtod(reinterpret_cast<const char*>(buf), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// IN clause in aggregate FILTER WHERE
// ---------------------------------------------------------------------------
TEST_CASE("SQL: COUNT FILTER WHERE col IN (literal list)") {
    auto p = fs::temp_directory_path() / "openads_sqlin_agg.dbf";
    fs::remove(p);
    // 4 rows: ACT/ACT/SUS pass the first filter; CLO passes the second.
    write_dbf(p,
        {{"STATUS", {'C', 3}}, {"AMT", {'N', 6}}},
        {{"ACT", "   100"}, {"SUS", "    50"}, {"ACT", "   200"}, {"CLO", "    10"}});

    std::string dir = p.parent_path().string();
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    double cnt_pass = run_agg_double(hConn,
        "SELECT COUNT(*) FILTER (WHERE STATUS IN ('ACT','SUS')) AS cnt "
        "FROM openads_sqlin_agg",
        "cnt");
    CHECK(cnt_pass == doctest::Approx(3.0));

    double cnt_clo = run_agg_double(hConn,
        "SELECT COUNT(*) FILTER (WHERE STATUS IN ('CLO')) AS cnt "
        "FROM openads_sqlin_agg",
        "cnt");
    CHECK(cnt_clo == doctest::Approx(1.0));

    AdsDisconnect(hConn);
    fs::remove(p);
}

// ---------------------------------------------------------------------------
// IS NULL / IS NOT NULL in aggregate FILTER WHERE
// ---------------------------------------------------------------------------
TEST_CASE("SQL: COUNT FILTER WHERE col IS NULL / IS NOT NULL") {
    auto p = fs::temp_directory_path() / "openads_sqlisnull.dbf";
    fs::remove(p);
    // Blank (all-spaces) character field is treated as NULL by OpenADS.
    write_dbf(p,
        {{"NAME", {'C', 5}}, {"VAL", {'N', 4}}},
        {{"Alice", "  42"}, {"     ", "   0"}, {"Bob  ", "  99"}, {"     ", "   1"}});

    std::string dir = p.parent_path().string();
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    double nulls = run_agg_double(hConn,
        "SELECT COUNT(*) FILTER (WHERE NAME IS NULL) AS cnt FROM openads_sqlisnull",
        "cnt");
    CHECK(nulls == doctest::Approx(2.0));

    double notnulls = run_agg_double(hConn,
        "SELECT COUNT(*) FILTER (WHERE NAME IS NOT NULL) AS cnt FROM openads_sqlisnull",
        "cnt");
    CHECK(notnulls == doctest::Approx(2.0));

    AdsDisconnect(hConn);
    fs::remove(p);
}

// ---------------------------------------------------------------------------
// BETWEEN in aggregate FILTER WHERE
// ---------------------------------------------------------------------------
TEST_CASE("SQL: SUM FILTER WHERE numeric BETWEEN lo AND hi") {
    auto p = fs::temp_directory_path() / "openads_sqlbetween.dbf";
    fs::remove(p);
    write_dbf(p,
        {{"SCORE", {'N', 5}}},
        {{"   10"}, {"   50"}, {"   70"}, {"  100"}, {"   30"}});

    std::string dir = p.parent_path().string();
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    // SUM of scores 30 <= x <= 70 → 50+70+30 = 150
    double s = run_agg_double(hConn,
        "SELECT SUM(SCORE) FILTER (WHERE SCORE BETWEEN 30 AND 70) AS s "
        "FROM openads_sqlbetween",
        "s");
    CHECK(s == doctest::Approx(150.0));

    AdsDisconnect(hConn);
    fs::remove(p);
}

// ---------------------------------------------------------------------------
// AdsEvalLogicalExpr
// ---------------------------------------------------------------------------
TEST_CASE("AdsEvalLogicalExpr evaluates AOF expression at current record") {
    auto p = fs::temp_directory_path() / "openads_evallogic.dbf";
    fs::remove(p);
    write_dbf(p,
        {{"NAME", {'C', 5}}, {"AGE", {'N', 3}}},
        {{"Alice", " 30"}, {"Bob  ", " 20"}});

    std::string dir  = p.parent_path().string();
    std::string base = p.filename().string();
    ADSHANDLE hConn = 0, hT = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn,
                         reinterpret_cast<UNSIGNED8*>(base.data()),
                         nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT) == 0);

    // Record 1 (Alice/30) — passes "AGE >= 25"
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED16 result = 99;
    UNSIGNED8 expr[] = "AGE >= 25";
    REQUIRE(AdsEvalLogicalExpr(hT, expr, &result) == 0);
    CHECK(result == 1);

    // Record 2 (Bob/20) — fails "AGE >= 25"
    REQUIRE(AdsSkip(hT, 1) == 0);
    result = 99;
    REQUIRE(AdsEvalLogicalExpr(hT, expr, &result) == 0);
    CHECK(result == 0);

    // Back to record 1: compound AND
    REQUIRE(AdsGotoTop(hT) == 0);
    result = 0;
    UNSIGNED8 expr2[] = "NAME = 'Alice' AND AGE >= 25";
    REQUIRE(AdsEvalLogicalExpr(hT, expr2, &result) == 0);
    CHECK(result == 1);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove(p);
}

// ---------------------------------------------------------------------------
// AdsEvalNumericExpr
// ---------------------------------------------------------------------------
TEST_CASE("AdsEvalNumericExpr reads field value or parses numeric literal") {
    auto p = fs::temp_directory_path() / "openads_evalnum.dbf";
    fs::remove(p);
    write_dbf(p,
        {{"AMT", {'N', 8}}},
        {{"   42.50"}});

    std::string dir  = p.parent_path().string();
    std::string base = p.filename().string();
    ADSHANDLE hConn = 0, hT = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn,
                         reinterpret_cast<UNSIGNED8*>(base.data()),
                         nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    double d = 0.0;
    UNSIGNED8 field[] = "AMT";
    REQUIRE(AdsEvalNumericExpr(hT, field, &d) == 0);
    CHECK(d == doctest::Approx(42.5));

    UNSIGNED8 lit[] = "3.14";
    d = 0.0;
    REQUIRE(AdsEvalNumericExpr(hT, lit, &d) == 0);
    CHECK(d == doctest::Approx(3.14));

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove(p);
}

// ---------------------------------------------------------------------------
// AdsEvalStringExpr
// ---------------------------------------------------------------------------
TEST_CASE("AdsEvalStringExpr reads field value or returns literal string") {
    auto p = fs::temp_directory_path() / "openads_evalstr.dbf";
    fs::remove(p);
    // Field width 8; as_string strips trailing spaces, so result is "Alice" (5).
    write_dbf(p,
        {{"NAME", {'C', 8}}},
        {{"Alice   "}});

    std::string dir  = p.parent_path().string();
    std::string base = p.filename().string();
    ADSHANDLE hConn = 0, hT = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn,
                         reinterpret_cast<UNSIGNED8*>(base.data()),
                         nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    UNSIGNED8 buf[64] = {};
    UNSIGNED16 len;

    // Field read — as_string strips trailing spaces → "Alice" (5 chars).
    UNSIGNED8 field[] = "NAME";
    len = static_cast<UNSIGNED16>(sizeof(buf));
    REQUIRE(AdsEvalStringExpr(hT, field, buf, &len) == 0);
    CHECK(len == 5);
    CHECK(std::string(reinterpret_cast<char*>(buf)) == "Alice");

    // Non-field literal — returned as-is.
    UNSIGNED8 lit[] = "Hello";
    len = static_cast<UNSIGNED16>(sizeof(buf));
    REQUIRE(AdsEvalStringExpr(hT, lit, buf, &len) == 0);
    CHECK(len == 5);
    CHECK(std::string(reinterpret_cast<char*>(buf)) == "Hello");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove(p);
}
