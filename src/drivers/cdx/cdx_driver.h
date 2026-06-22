#pragma once

#include "drivers/driver_trait.h"
#include "engine/aes.h"
#include "platform/file.h"

#include <array>
#include <optional>

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
    util::Result<bool> truncate_trailing(std::uint32_t recno) override;

    util::Result<std::uint32_t>
        bump_autoinc(std::uint16_t field_index) override;

    // M11.2 — encrypted DBF support. `encrypted()` reflects the
    // header version byte (0xC3 = OpenADS-encrypted variant);
    // `set_encryption_key` installs the AES-256 key the driver uses
    // to transparently encrypt / decrypt record bodies. Encrypts
    // every existing record on demand for plain → encrypted upgrade.
    bool encrypted() const noexcept { return encrypted_; }
    util::Result<void>
        set_encryption_key(const std::array<std::uint8_t, 32>& key);
    util::Result<void> encrypt_in_place(
        const std::array<std::uint8_t, 32>& key);

private:
    util::Result<void> rewrite_header_();
    // Re-read bytes 0..7 of the DBF header from disk and refresh
    // rec_count_ from the on-disk truth. Caller must hold an
    // exclusive byte-lock on the header before invoking, otherwise
    // the refresh races against other writers.
    util::Result<void> refresh_record_count_();
    void               apply_ctr_(std::uint8_t* buf, std::size_t n,
                                  std::uint32_t recno) const;

    platform::File              file_;
    std::vector<DbfField>       fields_;
    DriverOpenMode              mode_      = DriverOpenMode::ReadOnly;
    std::uint32_t               rec_count_ = 0;
    std::uint16_t               rec_len_   = 0;
    std::uint16_t               hdr_len_   = 0;
    // M11.2 — encryption state. encrypted_ mirrors the header byte;
    // aes_ is populated once the connection's password key is bound.
    bool                        encrypted_ = false;
    std::optional<engine::Aes>  aes_;
};

} // namespace openads::drivers::cdx
