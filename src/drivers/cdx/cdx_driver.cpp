#include "drivers/cdx/cdx_driver.h"

#include "platform/time.h"

#include <cstring>
#include <ctime>
#include <vector>

namespace openads::drivers::cdx {

namespace {

platform::OpenMode map_mode(DriverOpenMode m) {
    switch (m) {
        case DriverOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case DriverOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case DriverOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

} // namespace

util::Result<void>
CdxDriver::open(const std::string& path, DriverOpenMode mode) {
    mode_ = mode;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    std::uint8_t hdr_buf[32]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();
    if (got.value() < 32) {
        return util::Error{5103, 0, "DBF header truncated", path};
    }

    auto hdr = parse_dbf_header(hdr_buf, sizeof(hdr_buf));
    if (!hdr) return hdr.error();
    rec_count_ = hdr.value().record_count;
    rec_len_   = hdr.value().record_length;
    hdr_len_   = hdr.value().header_length;
    encrypted_ = hdr.value().encrypted;

    if (hdr_len_ < 33) {
        return util::Error{5103, 0, "DBF header length below 33 bytes", path};
    }
    std::size_t fd_size = hdr_len_ - 32;
    std::vector<std::uint8_t> fd_buf(fd_size, 0);
    auto fd_got = file_.read_at(32, fd_buf.data(), fd_buf.size());
    if (!fd_got) return fd_got.error();
    if (fd_got.value() < fd_buf.size()) {
        return util::Error{5103, 0, "field-descriptor block truncated", path};
    }
    auto fields = parse_dbf_fields(fd_buf.data(), fd_buf.size());
    if (!fields) return fields.error();
    fields_ = std::move(fields).value();
    return {};
}

util::Result<std::vector<std::uint8_t>>
CdxDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0 || recno > rec_count_) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    std::vector<std::uint8_t> buf(rec_len_, 0);
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto got = file_.read_at(offset, buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < buf.size()) {
        return util::Error{5000, 0, "short read on record body", ""};
    }
    if (encrypted_ && aes_) {
        apply_ctr_(buf.data(), buf.size(), recno);
    }
    return buf;
}

util::Result<void>
CdxDriver::write_record_raw(std::uint32_t recno,
                            const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (recno == 0 || recno > rec_count_) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    if (encrypted_ && aes_) {
        std::vector<std::uint8_t> tmp(buf, buf + n);
        apply_ctr_(tmp.data(), tmp.size(), recno);
        auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
        return {};
    }
    auto wrote = file_.write_at(offset, buf, n);
    if (!wrote) return wrote.error();
    if (wrote.value() != n) {
        return util::Error{5000, 0, "short write on record body", ""};
    }
    return {};
}

util::Result<std::uint32_t>
CdxDriver::append_record_raw(const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    std::uint32_t new_recno = rec_count_ + 1;
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(rec_count_) *
                           static_cast<std::uint64_t>(rec_len_);
    if (encrypted_ && aes_) {
        std::vector<std::uint8_t> tmp(buf, buf + n);
        apply_ctr_(tmp.data(), tmp.size(), new_recno);
        auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
    } else {
        auto wrote = file_.write_at(offset, buf, n);
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
    }
    std::uint8_t eof = 0x1A;
    file_.write_at(offset + n, &eof, 1);

    rec_count_ = new_recno;
    if (auto r = rewrite_header_(); !r) return r.error();
    return new_recno;
}

util::Result<void> CdxDriver::rewrite_header_() {
    std::uint8_t hdr_buf[32]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();

    std::int64_t now = platform::utc_unix_micros();
    std::time_t  secs = static_cast<std::time_t>(now / 1'000'000);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    hdr_buf[1] = static_cast<std::uint8_t>(tm_utc.tm_year);
    hdr_buf[2] = static_cast<std::uint8_t>(tm_utc.tm_mon + 1);
    hdr_buf[3] = static_cast<std::uint8_t>(tm_utc.tm_mday);

    hdr_buf[4] = static_cast<std::uint8_t>( rec_count_        & 0xFFu);
    hdr_buf[5] = static_cast<std::uint8_t>((rec_count_ >> 8)  & 0xFFu);
    hdr_buf[6] = static_cast<std::uint8_t>((rec_count_ >> 16) & 0xFFu);
    hdr_buf[7] = static_cast<std::uint8_t>((rec_count_ >> 24) & 0xFFu);

    auto wrote = file_.write_at(0, hdr_buf, sizeof(hdr_buf));
    if (!wrote) return wrote.error();
    if (wrote.value() != sizeof(hdr_buf)) {
        return util::Error{5000, 0, "short write on header", ""};
    }
    return {};
}

util::Result<void> CdxDriver::flush() {
    return file_.sync();
}

util::Result<void> CdxDriver::zap() {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    rec_count_ = 0;
    if (auto r = rewrite_header_(); !r) return r.error();
    // Place an EOF marker right after the field-descriptor block;
    // the records area on disk may still contain stale bytes but DBF
    // readers respect the header reccount.
    std::uint8_t eof = 0x1A;
    if (auto w = file_.write_at(hdr_len_, &eof, 1); !w) return w.error();
    return file_.sync();
}

util::Result<std::uint32_t>
CdxDriver::bump_autoinc(std::uint16_t field_index) {
    if (field_index >= fields_.size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    auto& f = fields_[field_index];
    if (!f.autoinc) {
        return util::Error{5063, 0, "field is not autoinc", f.name};
    }
    std::uint32_t curr = f.autoinc_next;
    std::uint32_t next = curr +
        static_cast<std::uint32_t>(f.autoinc_step ? f.autoinc_step : 1);
    f.autoinc_next = next;

    // Field-descriptor block starts at file offset 32; this field's
    // descriptor lives at 32 + 32*field_index; the autoinc-next bytes
    // sit at offset 19 within the descriptor.
    std::uint64_t off = 32u +
                        static_cast<std::uint64_t>(field_index) * 32u +
                        19u;
    std::uint8_t buf[4] = {
        static_cast<std::uint8_t>( next        & 0xFFu),
        static_cast<std::uint8_t>((next >>  8) & 0xFFu),
        static_cast<std::uint8_t>((next >> 16) & 0xFFu),
        static_cast<std::uint8_t>((next >> 24) & 0xFFu),
    };
    auto w = file_.write_at(off, buf, sizeof(buf));
    if (!w) return w.error();
    return curr;
}

void CdxDriver::apply_ctr_(std::uint8_t* buf, std::size_t n,
                           std::uint32_t recno) const {
    // M11.2 — AES-256-CTR over the record body. IV layout:
    // bytes 0..3 = recno LE, bytes 4..15 = block counter (LE).
    // Identical for encrypt and decrypt — XOR is symmetric.
    if (!aes_) return;
    std::array<std::uint8_t, 16> ctr{};
    ctr[0] = static_cast<std::uint8_t>( recno        & 0xFFu);
    ctr[1] = static_cast<std::uint8_t>((recno >>  8) & 0xFFu);
    ctr[2] = static_cast<std::uint8_t>((recno >> 16) & 0xFFu);
    ctr[3] = static_cast<std::uint8_t>((recno >> 24) & 0xFFu);
    for (std::size_t i = 0; i < n; i += 16) {
        std::array<std::uint8_t, 16> ks = ctr;
        aes_->encrypt_block(ks.data());
        std::size_t blk = std::min<std::size_t>(16, n - i);
        for (std::size_t k = 0; k < blk; ++k) {
            buf[i + k] ^= ks[k];
        }
        // Increment 12-byte block counter at bytes 4..15 (LE).
        for (std::size_t b = 4; b < 16; ++b) {
            if (++ctr[b] != 0) break;
        }
    }
}

util::Result<void>
CdxDriver::set_encryption_key(const std::array<std::uint8_t, 32>& key) {
    auto a = engine::Aes::from_key(engine::AesKeyBits::Aes256,
                                   std::vector<std::uint8_t>(
                                       key.begin(), key.end()));
    if (!a) return a.error();
    aes_ = std::move(a).value();
    return {};
}

util::Result<void>
CdxDriver::encrypt_in_place(const std::array<std::uint8_t, 32>& key) {
    // M11.2 — convert a plain DBF to OpenADS-encrypted: re-read every
    // existing record (still plain), set encrypted state, write each
    // record back through the encryption hook, flip header byte to
    // 0xC3 so future opens detect the format.
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (encrypted_) {
        return util::Error{5000, 0, "table is already encrypted", ""};
    }
    std::vector<std::vector<std::uint8_t>> plain;
    plain.reserve(rec_count_);
    for (std::uint32_t r = 1; r <= rec_count_; ++r) {
        auto rd = read_record_raw(r);
        if (!rd) return rd.error();
        plain.push_back(std::move(rd).value());
    }
    if (auto r = set_encryption_key(key); !r) return r.error();
    encrypted_ = true;
    for (std::uint32_t r = 1; r <= rec_count_; ++r) {
        if (auto w = write_record_raw(r, plain[r - 1].data(),
                                       plain[r - 1].size()); !w) {
            return w.error();
        }
    }
    std::uint8_t v = 0xC3;
    auto wh = file_.write_at(0, &v, 1);
    if (!wh) return wh.error();
    return {};
}

} // namespace openads::drivers::cdx
