#include "drivers/memory/memory_driver.h"

#include "openads/error.h"

#include <utility>

namespace openads::drivers::memory {

MemoryDriver::MemoryDriver(std::string path,
                           std::vector<DbfField> fields,
                           std::vector<std::vector<std::uint8_t>> records,
                           std::uint16_t record_length)
    : path_(std::move(path)),
      fields_(std::move(fields)),
      records_(std::move(records)),
      record_length_(record_length) {}

util::Result<void> MemoryDriver::open(const std::string& path,
                                      DriverOpenMode /*mode*/) {
    if (!path.empty()) path_ = path;
    return {};
}

std::uint32_t MemoryDriver::record_count() const noexcept {
    return static_cast<std::uint32_t>(records_.size());
}

util::Result<std::vector<std::uint8_t>>
MemoryDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0 || recno > records_.size()) {
        return util::Error{5000, 0, "record number out of range", path_};
    }
    return records_[static_cast<std::size_t>(recno - 1)];
}

util::Result<void>
MemoryDriver::write_record_raw(std::uint32_t /*recno*/,
                               const std::uint8_t* /*buf*/,
                               std::size_t /*n*/) {
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                       "memory table is read-only", path_};
}

util::Result<std::uint32_t>
MemoryDriver::append_record_raw(const std::uint8_t* /*buf*/,
                                std::size_t /*n*/) {
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                       "memory table is read-only", path_};
}

util::Result<void> MemoryDriver::flush() {
    return {};
}

util::Result<void> MemoryDriver::zap() {
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                       "memory table is read-only", path_};
}

} // namespace openads::drivers::memory
