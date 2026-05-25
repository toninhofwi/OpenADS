// openads_memo_stress — exercise FPT memo round-trip at scale.
//
// Pre-builds a DBF with header byte 0x83 (dBase III with memo) and a
// matching empty FPT sibling (FptMemo::create), opens it through the
// public ABI, appends N rows where each row has TWO memo fields
// (NOTES, DOCS) populated with payloads whose length cycles through
// {16, 256, 2048, 16384, 65536, 262144} bytes plus a deterministic
// 64-bit hash so the verify pass can confirm bit-perfect read-back.
//
// Usage: openads_memo_stress --rows N --dir DIR

#include "drivers/fpt/fpt_memo.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double, std::milli>(
               clock::now() - t0).count();
}

const std::uint32_t LENGTHS[] = {
    16, 256, 2048, 16384, 65536, 262144
};
constexpr std::size_t LENGTH_COUNT =
    sizeof(LENGTHS) / sizeof(LENGTHS[0]);

// Deterministic byte stream so the verify pass can recompute the
// expected payload without storing a copy.
std::uint8_t byte_at(std::uint64_t seed, std::uint32_t i) {
    std::uint64_t x = seed + i;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<std::uint8_t>(x & 0xFFu);
}

std::vector<std::uint8_t>
make_payload(std::uint64_t seed, std::uint32_t len) {
    std::vector<std::uint8_t> out(len);
    for (std::uint32_t i = 0; i < len; ++i) out[i] = byte_at(seed, i);
    return out;
}

std::uint64_t hash_payload(std::uint64_t seed, std::uint32_t len) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint32_t i = 0; i < len; ++i) {
        h ^= byte_at(seed, i);
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::uint64_t hash_bytes(const std::uint8_t* p, std::uint32_t len) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint32_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

// Hand-build a DBF with header 0x83 (memo flag) and three fields:
//   ID   N(8,0)   — 8 bytes
//   NAME C(16)    — 16 bytes
//   NOTES M(10)   — memo (10-byte block ptr)
//   DOCS  M(10)   — memo
fs::path build_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);

    constexpr int field_count = 4;
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * field_count + 1);
    std::uint16_t rec_len = 1 + 8 + 16 + 10 + 10;

    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x83;
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());

    auto add_field = [&](const char* name, char type,
                          std::uint8_t len, std::uint8_t dec) {
        std::array<std::uint8_t, 32> fd{};
        std::memcpy(fd.data(), name, std::min(std::strlen(name), std::size_t{11}));
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = len;
        fd[17] = dec;
        file.insert(file.end(), fd.begin(), fd.end());
    };
    add_field("ID",    'N', 8,  0);
    add_field("NAME",  'C', 16, 0);
    add_field("NOTES", 'M', 10, 0);
    add_field("DOCS",  'M', 10, 0);
    file.push_back(0x0D);
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

int main(int argc, char** argv) {
    std::uint32_t rows = 5000;
    std::string   data_dir = ".";
    bool          verify_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--rows" && i + 1 < argc)
            rows = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--dir" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--verify-only")         verify_only = true;
        else if (a == "-h" || a == "--help") {
            std::printf(
                "usage: %s [--rows N] [--dir DIR] [--verify-only]\n",
                argv[0]);
            return 0;
        }
    }
    fs::create_directories(data_dir);

    fs::path dbf_path = fs::path(data_dir) / "memo_stress.dbf";
    fs::path fpt_path = fs::path(data_dir) / "memo_stress.fpt";

    if (!verify_only) {
        std::error_code ec;
        fs::remove(fpt_path, ec);
        build_dbf(data_dir, "memo_stress.dbf");
        auto fpt_r = openads::drivers::fpt::FptMemo::create(
            fpt_path.string(), 64);
        if (!fpt_r) {
            std::fprintf(stderr, "FptMemo::create: %s\n",
                         fpt_r.error().message.c_str());
            return 1;
        }
    }

    std::vector<UNSIGNED8> srv(data_dir.size() + 1);
    std::memcpy(srv.data(), data_dir.c_str(), data_dir.size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "AdsConnect60 failed\n");
        return 1;
    }

    UNSIGNED8 leaf[64] = "memo_stress.dbf";
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        std::fprintf(stderr, "AdsOpenTable failed\n");
        return 1;
    }

    UNSIGNED8 fid[]    = "ID";
    UNSIGNED8 fname[]  = "NAME";
    UNSIGNED8 fnotes[] = "NOTES";
    UNSIGNED8 fdocs[]  = "DOCS";

    if (!verify_only) {
        std::printf("[memo] writing %u rows to %s\n",
                    rows, dbf_path.string().c_str());
        double t0 = now_ms();
        std::uint64_t bytes_written = 0;

        for (std::uint32_t r = 1; r <= rows; ++r) {
            if (auto rc = AdsAppendRecord(hTable); rc != 0) {
                std::fprintf(stderr, "Append rc=%u at r=%u\n", rc, r);
                return 1;
            }
            AdsSetDouble(hTable, fid, static_cast<double>(r));
            char namebuf[17] = {0};
            std::snprintf(namebuf, sizeof(namebuf), "row_%010u", r);
            AdsSetString(hTable, fname,
                         reinterpret_cast<UNSIGNED8*>(namebuf), 16);

            // NOTES: cycle by row.
            std::uint32_t nlen = LENGTHS[r % LENGTH_COUNT];
            std::uint64_t nseed = 0xA5A5'0000'0000'0000ULL ^ r;
            auto nbuf = make_payload(nseed, nlen);
            if (auto rc = AdsSetString(hTable, fnotes, nbuf.data(), nlen);
                rc != 0) {
                std::fprintf(stderr, "SetString NOTES rc=%u r=%u len=%u\n",
                             rc, r, nlen);
                return 1;
            }
            // DOCS: out-of-phase cycle, larger range.
            std::uint32_t dlen = LENGTHS[(r * 3 + 1) % LENGTH_COUNT];
            std::uint64_t dseed = 0x5A5A'0000'0000'0000ULL ^ r;
            auto dbuf = make_payload(dseed, dlen);
            if (auto rc = AdsSetString(hTable, fdocs, dbuf.data(), dlen);
                rc != 0) {
                std::fprintf(stderr, "SetString DOCS rc=%u r=%u len=%u\n",
                             rc, r, dlen);
                return 1;
            }
            bytes_written += nlen + dlen;
            if ((r % 500) == 0) {
                std::printf("  ...%u rows (%.1f s, %.1f MB written)\n",
                            r, (now_ms() - t0) / 1000.0,
                            static_cast<double>(bytes_written) / (1024.0 * 1024.0));
                std::fflush(stdout);
            }
        }
        AdsWriteRecord(hTable);
        std::printf("[memo] write phase done: %u rows, %.1f MB memo bytes,"
                    " %.1f s\n",
                    rows, static_cast<double>(bytes_written) / (1024.0 * 1024.0),
                    (now_ms() - t0) / 1000.0);
        AdsCloseTable(hTable);

        // Reopen for verify pass.
        if (AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         0, 0, 0, 0, &hTable) != 0) {
            std::fprintf(stderr, "Reopen failed\n");
            return 1;
        }
    }

    // Verify pass.
    UNSIGNED32 cnt = 0;
    AdsGetRecordCount(hTable, 0, &cnt);
    std::printf("[memo] verify: table reports %u records\n", cnt);
    double v0 = now_ms();
    std::uint32_t mismatches = 0;
    std::uint64_t total_bytes = 0;
    AdsGotoTop(hTable);
    for (std::uint32_t r = 1; r <= cnt; ++r) {
        AdsGotoRecord(hTable, r);
        UNSIGNED32 mlen = 0;
        AdsGetMemoLength(hTable, fnotes, &mlen);
        std::uint32_t exp_nlen = LENGTHS[r % LENGTH_COUNT];
        if (mlen != exp_nlen) {
            std::fprintf(stderr, "NOTES len mismatch r=%u got=%u exp=%u\n",
                         r, mlen, exp_nlen);
            ++mismatches;
            continue;
        }
        std::vector<UNSIGNED8> rd(mlen + 1, 0);
        UNSIGNED32 rlen = mlen + 1;
        UNSIGNED32 rcg = AdsGetField(hTable, fnotes, rd.data(), &rlen, 0);
        if (rlen != mlen) {
            std::fprintf(stderr,
                "NOTES rlen anomaly r=%u rc=%u rlen=%u mlen=%u\n",
                r, rcg, rlen, mlen);
        }
        std::uint64_t got = hash_bytes(rd.data(), mlen);
        std::uint64_t exp = hash_payload(0xA5A5'0000'0000'0000ULL ^ r, mlen);
        if (got != exp) {
            std::uint32_t first_diff = mlen;
            for (std::uint32_t k = 0; k < mlen; ++k) {
                std::uint8_t b = byte_at(0xA5A5'0000'0000'0000ULL ^ r, k);
                if (rd[k] != b) { first_diff = k; break; }
            }
            std::fprintf(stderr,
                "NOTES mismatch r=%u len=%u first_diff=%u\n",
                r, mlen, first_diff);
            ++mismatches;
        }
        total_bytes += mlen;

        AdsGetMemoLength(hTable, fdocs, &mlen);
        std::uint32_t exp_dlen = LENGTHS[(r * 3 + 1) % LENGTH_COUNT];
        if (mlen != exp_dlen) {
            std::fprintf(stderr, "DOCS len mismatch r=%u got=%u exp=%u\n",
                         r, mlen, exp_dlen);
            ++mismatches;
            continue;
        }
        rd.assign(mlen + 1, 0);
        rlen = mlen + 1;
        AdsGetField(hTable, fdocs, rd.data(), &rlen, 0);
        got = hash_bytes(rd.data(), mlen);
        exp = hash_payload(0x5A5A'0000'0000'0000ULL ^ r, mlen);
        if (got != exp) {
            std::uint32_t first_diff = mlen;
            for (std::uint32_t k = 0; k < mlen; ++k) {
                std::uint8_t b = byte_at(0x5A5A'0000'0000'0000ULL ^ r, k);
                if (rd[k] != b) { first_diff = k; break; }
            }
            std::fprintf(stderr,
                "DOCS mismatch r=%u len=%u first_diff=%u\n",
                r, mlen, first_diff);
            ++mismatches;
        }
        total_bytes += mlen;
        if ((r % 500) == 0) {
            std::printf("  ...verified %u rows (%.1f s, %.1f MB)\n",
                        r, (now_ms() - v0) / 1000.0,
                        static_cast<double>(total_bytes) / (1024.0 * 1024.0));
            std::fflush(stdout);
        }
    }
    std::printf("[memo] verify done: %u rows, %.1f MB read,"
                " %u mismatches, %.1f s\n",
                cnt, static_cast<double>(total_bytes) / (1024.0 * 1024.0),
                mismatches, (now_ms() - v0) / 1000.0);

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    return mismatches == 0 ? 0 : 1;
}
