#include "engine/tx_log.h"

#include <array>
#include <cstring>

namespace openads::engine {

namespace {

constexpr std::uint32_t WAL_MAGIC      = 0x004C4157u; // 'WAL\0' LE
constexpr std::size_t   WAL_HEADER_LEN = 24;          // magic..lsn inclusive

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
        auto cre = platform::File::open(path, platform::OpenMode::CreateRW);
        if (!cre) return cre.error();
        file_ = std::move(cre).value();
    } else {
        file_ = std::move(fres).value();
    }
    auto sz = file_.size();
    if (!sz) return sz.error();
    write_offset_ = sz.value();

    // Resume LSN counter from the highest LSN already on disk so newly
    // appended records can never collide with surviving ones.
    next_lsn_.store(1);
    last_synced_lsn_ = 0;
    if (write_offset_ > 0) {
        auto recs = read_all();
        if (recs) {
            std::uint64_t hi = 0;
            for (auto& r : recs.value()) if (r.lsn > hi) hi = r.lsn;
            next_lsn_.store(hi + 1);
            last_synced_lsn_ = hi;   // already on disk
        }
    }
    return {};
}

util::Result<std::uint64_t>
TxLog::append_record_(TxRecordType type, std::uint64_t tx_id,
                      const std::vector<std::uint8_t>& payload) {
    if (payload.size() > 0xFFFFu) {
        return util::Error{5000, 0, "tx log payload too large", ""};
    }

    std::lock_guard<std::mutex> lk(append_mu_);
    std::uint64_t lsn = next_lsn_.fetch_add(1);

    std::vector<std::uint8_t> rec(WAL_HEADER_LEN + payload.size() + 4, 0);
    write_u32_le(rec.data() + 0, WAL_MAGIC);
    rec[4] = static_cast<std::uint8_t>(type);
    rec[5] = 0;
    write_u16_le(rec.data() + 6, static_cast<std::uint16_t>(payload.size()));
    write_u64_le(rec.data() + 8,  tx_id);
    write_u64_le(rec.data() + 16, lsn);
    if (!payload.empty()) {
        std::memcpy(rec.data() + WAL_HEADER_LEN,
                    payload.data(), payload.size());
    }
    std::uint32_t crc = crc32c(rec.data(), WAL_HEADER_LEN + payload.size());
    write_u32_le(rec.data() + WAL_HEADER_LEN + payload.size(), crc);

    auto wrote = file_.write_at(write_offset_, rec.data(), rec.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != rec.size()) {
        return util::Error{5000, 0, "short write on tx log", ""};
    }
    write_offset_ += rec.size();
    return lsn;
}

util::Result<std::uint64_t> TxLog::append_begin_async(std::uint64_t tx_id) {
    return append_record_(TxRecordType::Begin, tx_id, {});
}

util::Result<std::uint64_t>
TxLog::append_update_async(std::uint64_t tx_id,
                           const std::string& table_path,
                           std::uint32_t recno,
                           const std::vector<std::uint8_t>& before,
                           const std::vector<std::uint8_t>& after) {
    std::size_t plen = 2 + table_path.size() + 4 + 2 + before.size()
                       + 2 + after.size();
    std::vector<std::uint8_t> payload(plen);
    std::uint8_t* p = payload.data();
    write_u16_le(p + 0, static_cast<std::uint16_t>(table_path.size()));
    if (!table_path.empty())
        std::memcpy(p + 2, table_path.data(), table_path.size());
    std::size_t off = 2 + table_path.size();
    write_u32_le(p + off, recno); off += 4;
    write_u16_le(p + off, static_cast<std::uint16_t>(before.size()));
    if (!before.empty()) std::memcpy(p + off + 2, before.data(), before.size());
    off += 2 + before.size();
    write_u16_le(p + off, static_cast<std::uint16_t>(after.size()));
    if (!after.empty()) std::memcpy(p + off + 2, after.data(), after.size());
    return append_record_(TxRecordType::Update, tx_id, payload);
}

util::Result<std::uint64_t> TxLog::append_commit_async(std::uint64_t tx_id) {
    return append_record_(TxRecordType::Commit, tx_id, {});
}

util::Result<std::uint64_t> TxLog::append_abort_async(std::uint64_t tx_id) {
    return append_record_(TxRecordType::Abort, tx_id, {});
}

util::Result<void> TxLog::append_begin(std::uint64_t tx_id) {
    auto r = append_begin_async(tx_id);
    if (!r) return r.error();
    return {};
}

util::Result<void>
TxLog::append_update(std::uint64_t tx_id,
                     const std::string& table_path,
                     std::uint32_t recno,
                     const std::vector<std::uint8_t>& before,
                     const std::vector<std::uint8_t>& after) {
    auto r = append_update_async(tx_id, table_path, recno, before, after);
    if (!r) return r.error();
    return {};
}

util::Result<void> TxLog::append_commit(std::uint64_t tx_id) {
    auto r = append_commit_async(tx_id);
    if (!r) return r.error();
    return sync_to(r.value());
}

util::Result<void> TxLog::append_abort(std::uint64_t tx_id) {
    auto r = append_abort_async(tx_id);
    if (!r) return r.error();
    return sync_to(r.value());
}

util::Result<void> TxLog::sync_to(std::uint64_t lsn) {
    // Fast-path: already durable.
    if (last_synced_lsn_ >= lsn) return {};

    std::lock_guard<std::mutex> lk(sync_mu_);
    if (last_synced_lsn_ >= lsn) return {};   // racing winner already synced

    // High-water mark BEFORE the fsync. Any record with an LSN <= hwm
    // is fully written by the time we issue the sync (append_mu_ has
    // released after each completed write_at).
    std::uint64_t hwm = next_lsn_.load();
    auto r = file_.sync();
    if (!r) return r.error();
    last_synced_lsn_ = hwm > 0 ? hwm - 1 : 0;
    return {};
}

util::Result<void> TxLog::sync() {
    std::lock_guard<std::mutex> lk(sync_mu_);
    std::uint64_t hwm = next_lsn_.load();
    auto r = file_.sync();
    if (!r) return r.error();
    last_synced_lsn_ = hwm > 0 ? hwm - 1 : 0;
    return {};
}

util::Result<std::vector<TxRecord>> TxLog::read_all() {
    std::vector<TxRecord> out;
    if (write_offset_ == 0) return out;

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(write_offset_), 0);
    auto got = file_.read_at(0, buf.data(), buf.size());
    if (!got) return got.error();
    std::size_t got_n = got.value();

    std::size_t pos = 0;
    while (pos + WAL_HEADER_LEN + 4 <= got_n) {
        if (read_u32_le(buf.data() + pos) != WAL_MAGIC) break;
        std::uint8_t  type_byte = buf[pos + 4];
        std::uint16_t plen      = read_u16_le(buf.data() + pos + 6);
        std::uint64_t tx_id     = read_u64_le(buf.data() + pos + 8);
        std::uint64_t lsn       = read_u64_le(buf.data() + pos + 16);
        std::size_t rec_size = WAL_HEADER_LEN + plen + 4;
        if (pos + rec_size > got_n) break;
        std::uint32_t stored_crc =
            read_u32_le(buf.data() + pos + WAL_HEADER_LEN + plen);
        std::uint32_t calc_crc =
            crc32c(buf.data() + pos, WAL_HEADER_LEN + plen);
        if (stored_crc != calc_crc) break;

        TxRecord r;
        r.type  = static_cast<TxRecordType>(type_byte);
        r.tx_id = tx_id;
        r.lsn   = lsn;
        if (r.type == TxRecordType::Update && plen >= 10) {
            const std::uint8_t* p = buf.data() + pos + WAL_HEADER_LEN;
            std::uint16_t pl = read_u16_le(p + 0);
            r.update.table_path.assign(reinterpret_cast<const char*>(p + 2), pl);
            std::size_t off = 2 + pl;
            r.update.recno = read_u32_le(p + off); off += 4;
            std::uint16_t bl = read_u16_le(p + off);
            r.update.before.assign(p + off + 2, p + off + 2 + bl);
            off += 2 + bl;
            std::uint16_t al = read_u16_le(p + off);
            r.update.after.assign(p + off + 2, p + off + 2 + al);
        }
        out.push_back(std::move(r));
        pos += rec_size;
    }
    return out;
}

util::Result<void> TxLog::truncate() {
    file_ = platform::File{};
    auto cre = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!cre) return cre.error();
    file_ = std::move(cre).value();
    write_offset_ = 0;
    next_lsn_.store(1);
    last_synced_lsn_ = 0;
    return {};
}

} // namespace openads::engine
