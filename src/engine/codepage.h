#pragma once

#include <cstdint>
#include <string>

namespace openads::engine {

// M11.8 — minimal OEM (CP437) ↔ UTF-8 conversion. The mapping
// covers the upper 128 bytes (0x80..0xFF) per the IBM PC CP437
// table; ASCII bytes (0x00..0x7F) pass through unchanged.

// Convert a CP437 byte buffer to UTF-8.
std::string cp437_to_utf8(const std::uint8_t* in, std::size_t n);

// Convert a UTF-8 string back to CP437. Codepoints not present in
// the table become '?' (0x3F).
std::string utf8_to_cp437(const char* in, std::size_t n);

} // namespace openads::engine
