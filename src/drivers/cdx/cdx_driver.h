#pragma once

#include "drivers/driver_trait.h"
#include "platform/file.h"

namespace openads::drivers::cdx {

class CdxDriver final : public IDriver {
public:
    util::Result<void> open(const std::string& path) override;

    std::uint32_t record_count() const noexcept override { return rec_count_; }
    std::uint16_t record_length() const noexcept override { return rec_len_; }
    const std::vector<DbfField>& fields() const noexcept override { return fields_; }

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override;

private:
    platform::File          file_;
    std::vector<DbfField>   fields_;
    std::uint32_t           rec_count_ = 0;
    std::uint16_t           rec_len_   = 0;
    std::uint16_t           hdr_len_   = 0;
};

} // namespace openads::drivers::cdx
