#pragma once

// Helpers for ADT on-disk header layout (unit tests only).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace adt_test {

namespace fs = std::filesystem;

inline fs::path repo_testdata_dir() {
    return fs::path(__FILE__).parent_path().parent_path().parent_path()
           / "testdata";
}

inline fs::path arc_ref_testdata_dir() {
    auto primary = repo_testdata_dir() / "arc_ref";
    if (fs::exists(primary / "animals.adt")) return primary;
    return {};
}

inline std::vector<std::uint8_t> read_file_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in), {});
}

struct AdtFieldInfo {
    std::string name;
    std::uint16_t offset = 0;
    std::uint16_t length = 0;
    std::uint16_t raw_type = 0;
    std::uint8_t  decimals = 0;
};

struct AdtHeaderInfo {
    std::string signature;
    std::uint32_t rec_count = 0;
    std::uint32_t hdr_len = 0;
    std::uint32_t rec_len = 0;
    std::vector<AdtFieldInfo> fields;
};

inline bool parse_adt_header(const std::vector<std::uint8_t>& bytes,
                             AdtHeaderInfo& out,
                             std::string& err) {
    if (bytes.size() < 400u) {
        err = "ADT header too short";
        return false;
    }
    out.signature.assign(reinterpret_cast<const char*>(bytes.data()), 15);
    auto r32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(bytes[off])
             | (static_cast<std::uint32_t>(bytes[off + 1]) << 8)
             | (static_cast<std::uint32_t>(bytes[off + 2]) << 16)
             | (static_cast<std::uint32_t>(bytes[off + 3]) << 24);
    };
    out.rec_count = r32(24);
    out.hdr_len   = r32(32);
    out.rec_len   = r32(36);
    if (out.hdr_len < 400u || out.hdr_len > bytes.size()) {
        err = "invalid hdr_len";
        return false;
    }
    const std::size_t nfields =
        (out.hdr_len - 400u) / 200u;
    out.fields.clear();
    out.fields.reserve(nfields);
    for (std::size_t i = 0; i < nfields; ++i) {
        const std::uint8_t* fd = bytes.data() + 400u + i * 200u;
        AdtFieldInfo fi;
        fi.name.assign(reinterpret_cast<const char*>(fd), 128);
        const auto nul = fi.name.find('\0');
        if (nul != std::string::npos) fi.name.resize(nul);
        fi.raw_type = static_cast<std::uint16_t>(fd[129])
                    | (static_cast<std::uint16_t>(fd[130]) << 8);
        fi.offset = static_cast<std::uint16_t>(fd[131])
                  | (static_cast<std::uint16_t>(fd[132]) << 8);
        fi.length = static_cast<std::uint16_t>(fd[135])
                  | (static_cast<std::uint16_t>(fd[136]) << 8);
        fi.decimals = fd[137];
        out.fields.push_back(std::move(fi));
    }
    return true;
}

inline bool signature_is_native_table(const AdtHeaderInfo& h) {
    return h.signature.rfind("Advantage Table", 0) == 0;
}

} // namespace adt_test