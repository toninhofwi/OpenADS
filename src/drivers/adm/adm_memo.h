#pragma once

#include "drivers/memo_trait.h"
#include "platform/file.h"

namespace openads::drivers::adm {

// Native memo store for .adm files (paired with .adt tables).
//
// Layout: 8-byte blocks (byte 7 of file header = 0x08).
// Payload is written verbatim at block_num * kBlockSize; length comes from
// the 9-byte in-record ADT reference (block_no + data_len + 0x00).
//
// In-record reference (9 bytes): uint32 LE block_num | uint32 LE data_len | 0x00.
// A reference with block_num == 0 means "no value".
//
// free_block() is a no-op (reclamation requires table PACK, not supported here).
class AdmMemo final : public IMemoStore {
public:
    static constexpr std::uint16_t kBlockSize = 8;
    // First 1024 bytes (128 x 8-byte blocks) reserved for .adm
    // metadata; payload blocks are numbered from kDataBlockOrigin upward.
    static constexpr std::uint32_t kDataBlockOrigin = 128;

    util::Result<void>
        open(const std::string& path, MemoOpenMode mode) override;

    util::Result<std::string>
        read(std::uint32_t block_no) override;

    // ADM-specific: data_len from the 9-byte in-record reference.
    util::Result<std::string>
        read(std::uint32_t block_no, std::uint32_t data_len) override;

    // Write payload into consecutive blocks starting at next_avail_.
    // Returns the assigned starting block number.
    util::Result<std::uint32_t>
        write(const std::string& payload) override;

    // No-op: ADM reclamation requires an external PACK operation.
    util::Result<void> free_block(std::uint32_t /*block_no*/) override {
        return {};
    }

    util::Result<void> flush() override;

    std::uint16_t block_size() const override { return kBlockSize; }

    // Build a fresh empty .adm. next_avail starts at 1 (block 0 = header).
    static util::Result<AdmMemo>
        create(const std::string& path);

private:
    util::Result<void> rewrite_header_();

    platform::File  file_;
    MemoOpenMode    mode_       = MemoOpenMode::ReadOnly;
    std::uint32_t   next_avail_ = 1;
};

} // namespace openads::drivers::adm
