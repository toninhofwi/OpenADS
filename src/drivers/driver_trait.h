#pragma once

#include "drivers/dbf_common.h"
#include "platform/file.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers {

// Minimal driver surface required by M1. Drivers will grow as later
// milestones add write, index, memo, encryption, and so on.
class IDriver {
public:
    virtual ~IDriver() = default;

    virtual util::Result<void>
        open(const std::string& path) = 0;

    virtual std::uint32_t record_count() const noexcept = 0;
    virtual std::uint16_t record_length() const noexcept = 0;
    virtual const std::vector<DbfField>& fields() const noexcept = 0;

    // Reads a single record's raw bytes (including the deletion byte).
    // recno is 1-based as in xBase. recno == 0 is invalid.
    virtual util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) = 0;
};

} // namespace openads::drivers
