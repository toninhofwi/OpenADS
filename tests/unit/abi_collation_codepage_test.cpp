#include "doctest.h"
#include "openads/ace.h"
#include "engine/codepage.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_dbf(const fs::path& path,
               const std::vector<std::pair<std::string, std::uint8_t>>& cols,
               const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(rows.size());
    std::uint16_t hl = static_cast<std::uint16_t>(32 + 32 * cols.size() + 1);
    std::uint16_t rl = 1;
    for (auto& c : cols) rl += c.second;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    for (auto& c : cols) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()),
                     c.first.c_str(), 11);
        fd[11] = 'C'; fd[16] = c.second;
        push(fd.data(), fd.size());
    }
    file.push_back(0x0D);
    for (auto& row : rows) {
        file.push_back(' ');
        for (std::size_t i = 0; i < cols.size(); ++i) {
            const auto& v = row[i];
            std::uint8_t L = cols[i].second;
            for (std::uint8_t k = 0; k < L; ++k) {
                file.push_back(k < v.size()
                    ? static_cast<std::uint8_t>(v[k]) : ' ');
            }
        }
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

}  // namespace

TEST_CASE("M11.7 NOCASE collation makes WHERE case-insensitive") {
    auto dir = fs::temp_directory_path() / "openads_m11_7";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "data.dbf", {{"NAME", 8}},
        {{"Alice"}, {"alice"}, {"ALICE"}, {"Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 mode[16] = "NOCASE";
    REQUIRE(AdsSetCollation(hConn, mode) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM data.dbf WHERE NAME = 'alice'";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);                          // Alice / alice / ALICE

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.8 CP437 ↔ UTF-8 round-trip") {
    using openads::engine::cp437_to_utf8;
    using openads::engine::utf8_to_cp437;

    // 0x82 (CP437 'é') → U+00E9 → 0xC3 0xA9
    std::uint8_t in[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x82};
    auto utf8 = cp437_to_utf8(in, sizeof(in));
    REQUIRE(utf8.size() == 8);                // 6 ASCII + 2 bytes for é
    CHECK(static_cast<std::uint8_t>(utf8[6]) == 0xC3);
    CHECK(static_cast<std::uint8_t>(utf8[7]) == 0xA9);

    auto back = utf8_to_cp437(utf8.data(), utf8.size());
    REQUIRE(back.size() == sizeof(in));
    for (std::size_t i = 0; i < sizeof(in); ++i) {
        CHECK(static_cast<std::uint8_t>(back[i]) == in[i]);
    }
}

TEST_CASE("M11.8 AdsConvertOemToAnsi / AdsConvertAnsiToOem round-trip") {
    UNSIGNED8 buf[32];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 'C'; buf[1] = 'a'; buf[2] = 'f';
    buf[3] = 0x82;                            // 'é' in CP437
    UNSIGNED32 len = 4;

    REQUIRE(AdsConvertOemToAnsi(buf, &len) == 0);
    CHECK(len == 5);                          // C a f + 2-byte UTF-8
    CHECK(buf[0] == 'C');
    CHECK(buf[1] == 'a');
    CHECK(buf[2] == 'f');
    CHECK(buf[3] == 0xC3);
    CHECK(buf[4] == 0xA9);

    REQUIRE(AdsConvertAnsiToOem(buf, &len) == 0);
    CHECK(len == 4);
    CHECK(buf[3] == 0x82);
}
