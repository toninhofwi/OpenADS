#include "engine/lsn_map.h"

#include "platform/file.h"

#include <cstring>
#include <filesystem>
#include <vector>

namespace openads::engine {

namespace {

constexpr std::uint32_t LSNM_MAGIC = 0x4D4E534Cu; // 'LSNM' LE

void write_u16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>( v       & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}
void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}
void write_u64_le(std::uint8_t* p, std::uint64_t v) {
    write_u32_le(p,     static_cast<std::uint32_t>( v        & 0xFFFFFFFFu));
    write_u32_le(p + 4, static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFu));
}
std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}
std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t read_u64_le(const std::uint8_t* p) {
    return  static_cast<std::uint64_t>(read_u32_le(p)) |
           (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
}

} // namespace

util::Result<void> LsnMap::open(const std::string& path) {
    path_    = path;
    entries_.clear();

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) return {};

    auto fres = platform::File::open(path, platform::OpenMode::ReadOnly);
    if (!fres) return fres.error();
    platform::File f = std::move(fres).value();

    auto sz = f.size();
    if (!sz) return sz.error();
    if (sz.value() < 8) return {};

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz.value()));
    auto got = f.read_at(0, buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < 8) return {};

    if (read_u32_le(buf.data() + 0) != LSNM_MAGIC) return {};
    std::uint32_t n = read_u32_le(buf.data() + 4);
    std::size_t   pos = 8;

    for (std::uint32_t i = 0; i < n; ++i) {
        if (pos + 2 > buf.size()) break;
        std::uint16_t pl = read_u16_le(buf.data() + pos);
        pos += 2;
        if (pos + pl + 12 > buf.size()) break;
        std::string path_s(reinterpret_cast<const char*>(buf.data() + pos), pl);
        pos += pl;
        std::uint32_t recno = read_u32_le(buf.data() + pos); pos += 4;
        std::uint64_t lsn   = read_u64_le(buf.data() + pos); pos += 8;
        entries_[Key{std::move(path_s), recno}] = lsn;
    }
    return {};
}

util::Result<void> LsnMap::flush() {
    namespace fs = std::filesystem;

    // Serialize.
    std::size_t total = 8;
    for (auto& [k, _] : entries_) {
        total += 2 + k.table_path.size() + 4 + 8;
    }
    std::vector<std::uint8_t> buf(total);
    write_u32_le(buf.data() + 0, LSNM_MAGIC);
    write_u32_le(buf.data() + 4, static_cast<std::uint32_t>(entries_.size()));
    std::size_t pos = 8;
    for (auto& [k, lsn] : entries_) {
        write_u16_le(buf.data() + pos,
                     static_cast<std::uint16_t>(k.table_path.size()));
        pos += 2;
        if (!k.table_path.empty()) {
            std::memcpy(buf.data() + pos, k.table_path.data(),
                        k.table_path.size());
            pos += k.table_path.size();
        }
        write_u32_le(buf.data() + pos, k.recno); pos += 4;
        write_u64_le(buf.data() + pos, lsn);     pos += 8;
    }

    // Atomic write: write to <path>.tmp, fsync, rename over <path>.
    std::string tmp = path_ + ".tmp";
    {
        auto fres = platform::File::open(tmp, platform::OpenMode::CreateRW);
        if (!fres) return fres.error();
        platform::File f = std::move(fres).value();
        auto wrote = f.write_at(0, buf.data(), buf.size());
        if (!wrote) return wrote.error();
        if (auto s = f.sync(); !s) return s.error();
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (ec) {
        // On Windows, rename() fails if target exists — remove + retry.
        fs::remove(path_, ec);
        fs::rename(tmp, path_, ec);
        if (ec) {
            return util::Error{5000, 0, "lsn_map flush rename failed", path_};
        }
    }
    return {};
}

std::uint64_t LsnMap::get(const std::string& table_path,
                          std::uint32_t      recno) const noexcept {
    auto it = entries_.find(Key{table_path, recno});
    return it == entries_.end() ? 0 : it->second;
}

void LsnMap::put(const std::string& table_path,
                 std::uint32_t      recno,
                 std::uint64_t      lsn) {
    auto& slot = entries_[Key{table_path, recno}];
    if (lsn > slot) slot = lsn;
}

void LsnMap::clear() {
    entries_.clear();
}

} // namespace openads::engine
