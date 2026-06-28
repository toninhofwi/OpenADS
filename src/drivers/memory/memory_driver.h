#pragma once

#include "drivers/driver_trait.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers::memory {

// RCB 06/28/2026: Read-only driver for small virtual result sets such as
// system.* metadata. We introduced this so DD metadata that is already in
// memory does not have to be written to _sys_*.adt scratch files and read
// back from disk just to satisfy a cursor. Keeping it behind IDriver lets the
// normal Table cursor path keep working: field metadata, record counts,
// navigation, EOF/BOF, AdsGetField, SQL WHERE, and ORDER BY all continue to
// use the same code paths as physical tables.
class MemoryDriver final : public IDriver {
public:
    MemoryDriver(std::string path,
                 std::vector<DbfField> fields,
                 std::vector<std::vector<std::uint8_t>> records,
                 std::uint16_t record_length);

    util::Result<void> open(const std::string& path,
                            DriverOpenMode mode) override;

    std::uint32_t record_count() const noexcept override;
    std::uint16_t record_length() const noexcept override { return record_length_; }
    std::uint16_t header_length() const noexcept override { return 0; }
    const std::vector<DbfField>& fields() const noexcept override { return fields_; }
    platform::File& file() override { return dummy_file_; }

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override;

    util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) override;

    util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) override;

    util::Result<void> flush() override;
    util::Result<void> zap() override;

private:
    std::string path_;
    std::vector<DbfField> fields_;
    std::vector<std::vector<std::uint8_t>> records_;
    std::uint16_t record_length_ = 0;
    platform::File dummy_file_;
};

} // namespace openads::drivers::memory
