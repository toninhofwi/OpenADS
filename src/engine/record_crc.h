#pragma once

#include <cstdint>
#include <vector>

namespace openads::engine {

// IEEE CRC-32 (reflected, poly 0xEDB88320) over raw record bytes — the
// same algorithm AdsGetRecordCRC uses for local tables.
inline std::uint32_t crc32_record(const std::vector<std::uint8_t>& buf) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t b : buf) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace openads::engine