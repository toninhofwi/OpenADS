#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openads::drivers {

enum class MemoOpenMode { ReadOnly, Shared, Exclusive };

// FPT block type tag, written into the 4-byte block header. The wire
// values match the FoxPro FPT layout (0 = picture/binary, 1 = text);
// the `Object` variant carries the same shape as a text block but
// flags a raw-binary payload that should NOT be sniffed as text.
enum class MemoBlockType : std::uint32_t {
    Picture = 0,
    Text    = 1,
    Object  = 2
};

// A MemoStore owns the secondary memo file (.dbt / .fpt / .adm).
// Drivers bind to one when the table opens; M-type fields decode their
// 10-byte ASCII block-number references into content via this interface.
class IMemoStore {
public:
    virtual ~IMemoStore() = default;

    virtual util::Result<void>
        open(const std::string& path, MemoOpenMode mode) = 0;

    // Read the memo at `block_no` (1-based; 0 means "no memo").
    virtual util::Result<std::string>
        read(std::uint32_t block_no) = 0;

    // ADM-specific overload: data_len is taken from the 9-byte in-record
    // reference since ADM blocks carry no per-block length header.
    // Default delegates to read(block_no) so FPT/DBT are unaffected.
    virtual util::Result<std::string>
        read(std::uint32_t block_no, std::uint32_t /*data_len*/) {
        return read(block_no);
    }

    // Read just the block's type tag (byte-stream is unchanged).
    // Default is Text; FPT-backed stores override to inspect the
    // actual on-disk header byte.
    virtual util::Result<MemoBlockType>
        read_type(std::uint32_t /*block_no*/) {
        return MemoBlockType::Text;
    }

    // Allocate and write a memo. Returns the assigned block number.
    virtual util::Result<std::uint32_t>
        write(const std::string& payload) = 0;

    // Allocate + write a memo with an explicit type tag. Default
    // delegates to write() (which always emits Text); FPT overrides.
    virtual util::Result<std::uint32_t>
        write_typed(const std::string& payload, MemoBlockType /*type*/) {
        return write(payload);
    }

    // Mark a memo's blocks as free.
    virtual util::Result<void> free_block(std::uint32_t block_no) = 0;

    virtual util::Result<void> flush() = 0;

    virtual std::uint16_t block_size() const = 0;
};

} // namespace openads::drivers
