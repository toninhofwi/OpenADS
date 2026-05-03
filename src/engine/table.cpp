#include "engine/table.h"

#include "drivers/cdx/cdx_driver.h"

#include <utility>

namespace openads::engine {

util::Result<Table> Table::open(const std::string& path, TableType type) {
    std::unique_ptr<drivers::IDriver> drv;
    switch (type) {
        case TableType::Cdx:
            drv = std::make_unique<drivers::cdx::CdxDriver>();
            break;
        case TableType::Ntx:
        case TableType::Adt:
        case TableType::Vfp:
            return util::Error{5004, 0,
                               "table type not yet supported in M1", path};
    }
    if (auto r = drv->open(path); !r) return r.error();
    return Table{std::move(drv)};
}

std::uint16_t Table::field_count() const noexcept {
    return static_cast<std::uint16_t>(driver_->fields().size());
}

const drivers::DbfField& Table::field_descriptor(std::uint16_t idx) const {
    return driver_->fields().at(idx);
}

std::uint32_t Table::record_count() const noexcept {
    return driver_->record_count();
}

util::Result<void> Table::load_record_(std::uint32_t recno) {
    auto buf = driver_->read_record_raw(recno);
    if (!buf) return buf.error();
    record_buf_ = std::move(buf).value();
    recno_      = recno;
    state_      = State::Positioned;
    return {};
}

util::Result<void> Table::goto_top() {
    if (driver_->record_count() == 0) {
        state_ = State::Eof;
        recno_ = 0;
        return {};
    }
    return load_record_(1);
}

util::Result<void> Table::goto_bottom() {
    auto n = driver_->record_count();
    if (n == 0) {
        state_ = State::Eof;
        recno_ = 0;
        return {};
    }
    return load_record_(n);
}

util::Result<void> Table::goto_record(std::uint32_t recno) {
    if (recno == 0 || recno > driver_->record_count()) {
        state_ = State::Eof;
        recno_ = 0;
        return util::Error{5000, 0, "recno out of range", ""};
    }
    return load_record_(recno);
}

util::Result<void> Table::skip(std::int32_t delta) {
    auto n = driver_->record_count();
    if (n == 0) { state_ = State::Eof; recno_ = 0; return {}; }

    std::int64_t target = static_cast<std::int64_t>(recno_) + delta;
    if (state_ == State::Bof && delta > 0) target = delta;

    if (target < 1) {
        state_ = State::Bof;
        recno_ = 0;
        return {};
    }
    if (target > static_cast<std::int64_t>(n)) {
        state_ = State::Eof;
        recno_ = n + 1;
        return {};
    }
    return load_record_(static_cast<std::uint32_t>(target));
}

util::Result<drivers::DbfFieldValue>
Table::read_field(std::uint16_t field_index) {
    if (state_ != State::Positioned) {
        return util::Error{5000, 0, "table not positioned on a record", ""};
    }
    if (field_index >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    return drivers::decode_field(driver_->fields().at(field_index),
                                 record_buf_.data(), record_buf_.size());
}

} // namespace openads::engine
