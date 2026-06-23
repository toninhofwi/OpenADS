// Index-expression validation: a bare identifier that is not a column must
// fail at AdsCreateIndex* time (native Harbour/Clipper raises "Variable does
// not exist"), instead of silently building an all-blank, useless index that
// hides a typo / renamed field in the caller's PRG.
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("BBBB"); rec("AAAA"); rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("AdsCreateIndex61 rejects a bare expression naming an unknown column") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr_validation";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[16] = "data";

    // 1) A valid bare-field expression still succeeds.
    {
        UNSIGNED8 tag[16]  = "OKTAG";
        UNSIGNED8 expr[16] = "TAG";
        ADSHANDLE hIdx = 0;
        CHECK(AdsCreateIndex61(hTable, idxfile, tag, expr,
                               nullptr, nullptr, 0, 512, &hIdx) == 0);
        if (hIdx) AdsCloseIndex(hIdx);
    }

    // 2) A bare identifier that is NOT a column must fail (was silently
    //    accepted before, building an all-blank index).
    {
        UNSIGNED8 tag[16]  = "BADTAG";
        UNSIGNED8 expr[16] = "NOFIELD";
        ADSHANDLE hIdx = 0;
        CHECK(AdsCreateIndex61(hTable, idxfile, tag, expr,
                               nullptr, nullptr, 0, 512, &hIdx) != 0);
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
