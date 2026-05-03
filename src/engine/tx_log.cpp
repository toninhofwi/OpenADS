#include "engine/tx_log.h"

#include <array>
#include <cstring>

namespace openads::engine {

namespace {

constexpr std::uint32_t WAL_MAGIC = 0x004C4157u; // 'WAL\0' LE

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
    return static_cast<std::uint64_t>(read_u32_le(p)) |
          (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
}

// CRC-32C (Castagnoli polynomial 0x1EDC6F41 reversed = 0x82F63B78).
std::uint32_t crc32c(const std::uint8_t* data, std::size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0x82F63B78u & mask);
        }
    }
    return ~crc;
}

} // namespace

util::Result<void> TxLog::open(const std::string& path) {
    path_ = path;
    auto fres = platform::File::open(path, platform::OpenMode::ReadWrite);
    if (!fres) {
        // Try create.
        auto cre = platform::File::open(path, platform::OpenMode::CreateRW);
        if (!cre) return cre.error();
        file_ = std::move(cre).value();
    } else {
        file_ = std::move(fres).value();
    }
    auto sz = file_.size();
    if (!sz) return sz.error();
    write_offset_ = sz.value();
    return {};
}

util::Result<void>
TxLog::append_record_(TxRecordType type, std::uint64_t tx_id,
                      const std::vector<std::uint8_t>& payload) {
    if (payload.size() > 0xFFFFu) {
        return util::Error{5000, 0, "tx log payload too large", ""};
    }
    std::vector<std::uint8_t> rec(16 + payload.size() + 4, 0);
    write_u32_le(rec.data() + 0, WAL_MAGIC);
    rec[4] = static_cast<std::uint8_t>(type);
    rec[5] = 0;
    write_u16_le(rec.data() + 6, static_cast<std::uint16_t>(payload.size()));
    write_u64_le(rec.data() + 8, tx_id);
    if (!payload.empty()) {
        std::memcpy(rec.data() + 16, payload.data(), payload.size());
    }
    std::uint32_t crc = crc32c(rec.data(), 16 + payload.size());
    write_u32_le(rec.data() + 16 + payload.size(), crc);

    auto wrote = file_.write_at(write_offset_, rec.data(), rec.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != rec.size()) {
        return util::Error{5000, 0, "short write on tx log", ""};
    }
    write_offset_ += rec.size();
    return {};
}

util::Result<void> TxLog::append_begin(std::uint64_t tx_id) {
    return append_record_(TxRecordType::Begin, tx_id, {});
}

util::Result<void>
TxLog::append_update(std::uint64_t tx_id,
                     std::uint32_t table_id, std::uint32_t recno,
                     const std::vector<std::uint8_t>& before,
                     const std::vector<std::uint8_t>& after) {
    std::vector<std::uint8_t> payload(4 + 4 + 2 + before.size() + 2 + after.size());
    std::uint8_t* p = payload.data();
    write_u32_le(p + 0, table_id);
    write_u32_le(p + 4, recno);
    write_u16_le(p + 8, static_cast<std::uint16_t>(before.size()));
    if (!before.empty()) std::memcpy(p + 10, before.data(), before.size());
    std::size_t off = 10 + before.size();
    write_u16_le(p + off, static_cast<std::uint16_t>(after.size()));
    if (!after.empty()) std::memcpy(p + off + 2, after.data(), after.size());
    return append_record_(TxRecordType::Update, tx_id, payload);
}

util::Result<void> TxLog::append_commit(std::uint64_t tx_id) {
    auto r = append_record_(TxRecordType::Commit, tx_id, {});
    if (!r) return r.error();
    return file_.sync();
}

util::Result<void> TxLog::append_abort(std::uint64_t tx_id) {
    auto r = append_record_(TxRecordType::Abort, tx_id, {});
    if (!r) return r.error();
    return file_.sync();
}

util::Result<void> TxLog::sync() {
    return file_.sync();
}

util::Result<std::vector<TxRecord>> TxLog::read_all() {
    std::vector<TxRecord> out;
    if (write_offset_ == 0) return out;

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(write_offset_), 0);
    auto got = file_.read_at(0, buf.data(), buf.size());
    if (!got) return got.error();
    std::size_t got_n = got.value();

    std::size_t pos = 0;
    while (pos + 20 <= got_n) {
        if (read_u32_le(buf.data() + pos) != WAL_MAGIC) break;
        std::uint8_t  type_byte = buf[pos + 4];
        std::uint16_t plen      = read_u16_le(buf.data() + pos + 6);
        std::uint64_t tx_id     = read_u64_le(buf.data() + pos + 8);
        std::size_t rec_size = 16 + plen + 4;
        if (pos + rec_size > got_n) break;
        std::uint32_t stored_crc = read_u32_le(buf.data() + pos + 16 + plen);
        std::uint32_t calc_crc   = crc32c(buf.data() + pos, 16 + plen);
        if (stored_crc != calc_crc) break;

        TxRecord r;
        r.type  = static_cast<TxRecordType>(type_byte);
        r.tx_id = tx_id;
        if (r.type == TxRecordType::Update && plen >= 12) {
            const std::uint8_t* p = buf.data() + pos + 16;
            r.update.table_id = read_u32_le(p + 0);
            r.update.recno    = read_u32_le(p + 4);
            std::uint16_t bl  = read_u16_le(p + 8);
            r.update.before.assign(p + 10, p + 10 + bl);
            std::size_t off = 10 + bl;
            std::uint16_t al = read_u16_le(p + off);
            r.update.after.assign(p + off + 2, p + off + 2 + al);
        }
        out.push_back(std::move(r));
        pos += rec_size;
    }
    return out;
}

util::Result<void> TxLog::truncate() {
    // Re-open with CreateRW to truncate.
    file_ = platform::File{};
    auto cre = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!cre) return cre.error();
    file_ = std::move(cre).value();
    write_offset_ = 0;
    return {};
}

} // namespace openads::engine
