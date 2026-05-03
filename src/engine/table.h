#pragma once

#include "drivers/driver_trait.h"
#include "drivers/dbf_common.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openads::engine {

enum class TableType { Cdx, Ntx, Adt, Vfp };

class Table {
public:
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) noexcept = default;
    Table& operator=(Table&&) noexcept = default;
    ~Table() = default;

    static util::Result<Table> open(const std::string& path, TableType type);

    std::uint16_t field_count() const noexcept;
    const drivers::DbfField& field_descriptor(std::uint16_t idx) const;

    std::uint32_t record_count() const noexcept;
    std::uint32_t recno() const noexcept { return recno_; }
    bool eof() const noexcept { return state_ == State::Eof; }
    bool bof() const noexcept { return state_ == State::Bof; }

    util::Result<void> goto_top();
    util::Result<void> goto_bottom();
    util::Result<void> goto_record(std::uint32_t recno);
    util::Result<void> skip(std::int32_t delta);

    util::Result<drivers::DbfFieldValue>
        read_field(std::uint16_t field_index);

private:
    enum class State { Bof, Positioned, Eof };

    explicit Table(std::unique_ptr<drivers::IDriver> drv) noexcept
        : driver_(std::move(drv)) {}

    util::Result<void> load_record_(std::uint32_t recno);

    std::unique_ptr<drivers::IDriver> driver_;
    State                             state_  = State::Bof;
    std::uint32_t                     recno_  = 0;
    std::vector<std::uint8_t>         record_buf_;
};

} // namespace openads::engine
