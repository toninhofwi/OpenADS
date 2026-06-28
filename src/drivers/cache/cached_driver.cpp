#include "drivers/cache/cached_driver.h"

#include "openads/error.h"

#include <algorithm>
#include <utility>

namespace openads::drivers::cache {

CachedDriver::CachedDriver(std::unique_ptr<IDriver> inner,
                           TableCacheMode mode)
    : inner_(std::move(inner)), mode_(mode) {}

util::Result<std::unique_ptr<CachedDriver>>
CachedDriver::create(std::unique_ptr<IDriver> inner,
                     TableCacheMode mode,
                     std::size_t max_bytes) {
    if (!inner || mode == TableCacheMode::None) {
        return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                           "table cache disabled", ""};
    }
    auto out = std::unique_ptr<CachedDriver>(
        new CachedDriver(std::move(inner), mode));
    if (auto r = out->load_all_(max_bytes); !r) return r.error();
    return std::move(out);
}

util::Result<void> CachedDriver::load_all_(std::size_t max_bytes) {
    // RCB 06/28/2026: DD table caching is intentionally capped so marking a
    // large table cannot accidentally consume unbounded process memory. If the
    // table is above the safe threshold, Connection::open_table leaves it on
    // the normal disk driver rather than failing the user's open.
    const auto count = inner_->record_count();
    const auto len = inner_->record_length();
    const auto bytes = static_cast<std::uint64_t>(count) *
                       static_cast<std::uint64_t>(len);
    if (bytes > max_bytes) {
        return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                           "table too large for in-memory cache", ""};
    }

    records_.clear();
    records_.reserve(count);
    for (std::uint32_t r = 1; r <= count; ++r) {
        auto rec = inner_->read_record_raw(r);
        if (!rec) return rec.error();
        records_.push_back(std::move(rec).value());
    }
    return {};
}

util::Result<void> CachedDriver::open(const std::string& path,
                                      DriverOpenMode mode) {
    if (auto r = inner_->open(path, mode); !r) return r.error();
    return load_all_(kDefaultMaxBytes);
}

std::uint32_t CachedDriver::record_count() const noexcept {
    return static_cast<std::uint32_t>(records_.size());
}

std::uint16_t CachedDriver::record_length() const noexcept {
    return inner_->record_length();
}

std::uint16_t CachedDriver::header_length() const noexcept {
    return inner_->header_length();
}

const std::vector<DbfField>& CachedDriver::fields() const noexcept {
    return inner_->fields();
}

platform::File& CachedDriver::file() {
    return inner_->file();
}

util::Result<std::vector<std::uint8_t>>
CachedDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0 || recno > records_.size()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "record number out of range", ""};
    }
    return records_[static_cast<std::size_t>(recno - 1)];
}

util::Result<void>
CachedDriver::write_record_raw(std::uint32_t recno,
                               const std::uint8_t* buf,
                               std::size_t n) {
    // RCB 06/28/2026: Writes are write-through even for
    // ADS_TABLE_CACHE_WRITES. That gives cached reads now while avoiding a
    // delayed-write window where data pages, memo pages, and indexes could get
    // out of sync. A true write-behind mode can be added later with explicit
    // index/memo journaling rules.
    if (recno == 0 || recno > records_.size()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "record number out of range", ""};
    }
    if (n != record_length()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "record buffer length mismatch", ""};
    }
    auto r = inner_->write_record_raw(recno, buf, n);
    if (!r) return r.error();
    records_[static_cast<std::size_t>(recno - 1)].assign(buf, buf + n);
    return {};
}

util::Result<std::uint32_t>
CachedDriver::append_record_raw(const std::uint8_t* buf, std::size_t n) {
    if (n != record_length()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "record buffer length mismatch", ""};
    }
    auto appended = inner_->append_record_raw(buf, n);
    if (!appended) return appended.error();
    const auto recno = appended.value();
    if (recno == records_.size() + 1) {
        records_.emplace_back(buf, buf + n);
    } else {
        if (auto r = load_all_(kDefaultMaxBytes); !r) return r.error();
    }
    return recno;
}

util::Result<void> CachedDriver::flush() {
    return inner_->flush();
}

util::Result<void> CachedDriver::zap() {
    auto r = inner_->zap();
    if (!r) return r.error();
    records_.clear();
    return {};
}

util::Result<bool> CachedDriver::truncate_trailing(std::uint32_t recno) {
    auto r = inner_->truncate_trailing(recno);
    if (!r) return r.error();
    if (r.value() && recno == records_.size()) {
        records_.pop_back();
    } else if (r.value()) {
        if (auto reload = load_all_(kDefaultMaxBytes); !reload) return reload.error();
    }
    return r.value();
}

util::Result<std::uint32_t>
CachedDriver::bump_autoinc(std::uint16_t field_index) {
    return inner_->bump_autoinc(field_index);
}

} // namespace openads::drivers::cache
