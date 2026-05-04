#pragma once

#include "drivers/driver_trait.h"
#include "platform/file.h"

namespace openads::drivers::cdx {

class CdxDriver final : public IDriver {
public:
    util::Result<void>
        open(const std::string& path, DriverOpenMode mode) override;

    std::uint32_t record_count() const noexcept override { return rec_count_; }
    std::uint16_t record_length() const noexcept override { return rec_len_; }
    std::uint16_t header_length() const noexcept override { return hdr_len_; }
    const std::vector<DbfField>& fields() const noexcept override { return fields_; }
    platform::File& file() override { return file_; }

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override;

    util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) override;

    util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) override;

    util::Result<void> flush() override;
    util::Result<void> zap()   override;

private:
    util::Result<void> rewrite_header_();

    platform::File          file_;
    std::vector<DbfField>   fields_;
    DriverOpenMode          mode_      = DriverOpenMode::ReadOnly;
    std::uint32_t           rec_count_ = 0;
    std::uint16_t           rec_len_   = 0;
    std::uint16_t           hdr_len_   = 0;
};

} // namespace openads::drivers::cdx
