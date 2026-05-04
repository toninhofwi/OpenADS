#pragma once

#include "drivers/memo_trait.h"

#include <string>

namespace openads::drivers::fpt {

// FoxPro / VFP FPT memo store. Header stores next-available-block and
// block size as **big-endian** uint32/uint16. Each memo block has an
// 8-byte header (type[4] + length[4], both BE) followed by the payload.
class FptMemo final : public IMemoStore {
public:
    util::Result<void>
        open(const std::string& path, MemoOpenMode mode) override;

    util::Result<std::string>
        read(std::uint32_t block_no) override;

    util::Result<MemoBlockType>
        read_type(std::uint32_t block_no) override;

    util::Result<std::uint32_t>
        write(const std::string& payload) override;

    util::Result<std::uint32_t>
        write_typed(const std::string& payload, MemoBlockType type) override;

    util::Result<void> free_block(std::uint32_t block_no) override;
    util::Result<void> flush() override;

    std::uint16_t block_size() const override { return block_size_; }

    // Build a fresh empty .fpt with the given block size (default 64,
    // matching VFP). header_block is always 1 page (512 bytes header).
    static util::Result<FptMemo>
        create(const std::string& path, std::uint16_t block_size = 64);

private:
    util::Result<void> rewrite_header_();

    platform::File  file_;
    MemoOpenMode    mode_       = MemoOpenMode::ReadOnly;
    std::uint32_t   next_avail_ = 8;   // first 8 blocks reserved for header
    std::uint16_t   block_size_ = 64;
};

} // namespace openads::drivers::fpt
