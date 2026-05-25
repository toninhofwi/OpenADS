// openads_memo_zip — round-trip a real .zip file through an FPT memo
// field. Writes the entire .zip blob into BLOB M(10) on a single
// record, reopens the table, reads the memo back, and dumps it to
// the requested output path. The caller validates archive integrity
// (e.g., python -m zipfile -t out.zip).

#include "drivers/fpt/fpt_memo.h"
#include "openads/ace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path build_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "zipholder.dbf";
    fs::remove(p);

    constexpr int field_count = 2;
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * field_count + 1);
    std::uint16_t rec_len = 1 + 16 + 10;

    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x83;
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());

    auto add = [&](const char* name, char type, std::uint8_t len) {
        std::array<std::uint8_t, 32> fd{};
        std::memcpy(fd.data(), name, std::min(std::strlen(name), std::size_t{11}));
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = len;
        file.insert(file.end(), fd.begin(), fd.end());
    };
    add("LABEL", 'C', 16);
    add("BLOB",  'M', 10);
    file.push_back(0x0D);
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::uint64_t fnv1a(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

} // namespace

int main(int argc, char** argv) {
    std::string zip_in;
    std::string zip_out = "memo_roundtrip.zip";
    std::string data_dir = ".";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--zip-in"  && i + 1 < argc) zip_in   = argv[++i];
        else if (a == "--zip-out" && i + 1 < argc) zip_out  = argv[++i];
        else if (a == "--dir"     && i + 1 < argc) data_dir = argv[++i];
    }
    if (zip_in.empty()) {
        std::fprintf(stderr,
            "usage: %s --zip-in PATH [--zip-out PATH] [--dir DIR]\n",
            argv[0]);
        return 1;
    }

    std::ifstream zin(zip_in, std::ios::binary | std::ios::ate);
    if (!zin) { std::fprintf(stderr, "cannot open %s\n", zip_in.c_str()); return 1; }
    auto sz = zin.tellg();
    zin.seekg(0);
    std::vector<std::uint8_t> zip_bytes(static_cast<std::size_t>(sz));
    zin.read(reinterpret_cast<char*>(zip_bytes.data()),
             static_cast<std::streamsize>(zip_bytes.size()));
    if (zip_bytes.size() < 4 ||
        !(zip_bytes[0] == 'P' && zip_bytes[1] == 'K')) {
        std::fprintf(stderr, "input is not a ZIP (no PK signature)\n");
        return 1;
    }
    std::uint64_t in_hash = fnv1a(zip_bytes.data(), zip_bytes.size());
    std::printf("[zip] read %zu bytes from %s, fnv1a=%016llx\n",
                zip_bytes.size(), zip_in.c_str(),
                static_cast<unsigned long long>(in_hash));

    fs::create_directories(data_dir);
    fs::path dbf_path = build_dbf(data_dir);
    fs::path fpt_path = fs::path(data_dir) / "zipholder.fpt";
    {
        std::error_code ec;
        fs::remove(fpt_path, ec);
    }
    auto fpt_r = openads::drivers::fpt::FptMemo::create(fpt_path.string(), 64);
    if (!fpt_r) { std::fprintf(stderr, "FptMemo::create failed\n"); return 1; }

    std::vector<UNSIGNED8> srv(data_dir.size() + 1);
    std::memcpy(srv.data(), data_dir.c_str(), data_dir.size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "AdsConnect60 failed\n");
        return 1;
    }

    UNSIGNED8 leaf[64] = "zipholder.dbf";
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        std::fprintf(stderr, "AdsOpenTable failed\n");
        return 1;
    }
    if (AdsAppendRecord(hTable) != 0) return 1;
    UNSIGNED8 flabel[16] = "LABEL";
    UNSIGNED8 lab[17] = "ROUND_TRIP_ZIP  ";
    AdsSetString(hTable, flabel, lab, 16);

    UNSIGNED8 fblob[16] = "BLOB";
    if (auto rc = AdsSetString(hTable, fblob, zip_bytes.data(),
                                static_cast<UNSIGNED32>(zip_bytes.size()));
        rc != 0) {
        std::fprintf(stderr, "AdsSetString rc=%u\n", rc); return 1;
    }
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);

    if (AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        std::fprintf(stderr, "Reopen failed\n"); return 1;
    }
    AdsGotoTop(hTable);

    UNSIGNED32 mlen = 0;
    AdsGetMemoLength(hTable, fblob, &mlen);
    std::printf("[zip] memo length on read = %u (expected %zu)\n",
                mlen, zip_bytes.size());

    std::vector<UNSIGNED8> rd(mlen + 1, 0);
    UNSIGNED32 rlen = mlen + 1;
    if (auto rc = AdsGetField(hTable, fblob, rd.data(), &rlen, 0); rc != 0) {
        std::fprintf(stderr, "AdsGetField rc=%u\n", rc); return 1;
    }
    std::printf("[zip] read back %u bytes\n", rlen);

    std::uint64_t out_hash = fnv1a(rd.data(), rlen);
    bool hash_ok = (out_hash == in_hash) && (rlen == zip_bytes.size());
    std::printf("[zip] fnv1a(out)=%016llx %s\n",
                static_cast<unsigned long long>(out_hash),
                hash_ok ? "MATCH" : "MISMATCH");

    std::ofstream zout(zip_out, std::ios::binary);
    zout.write(reinterpret_cast<const char*>(rd.data()),
               static_cast<std::streamsize>(rlen));
    zout.close();
    std::printf("[zip] wrote round-tripped archive to %s\n", zip_out.c_str());

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    return hash_ok ? 0 : 1;
}
