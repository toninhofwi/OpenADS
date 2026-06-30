#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace openads::engine {

// PBKDF2-HMAC-SHA256 (RFC 8018). Used for OpenADS encrypted tables (0xC4).
std::array<std::uint8_t, 32>
pbkdf2_sha256(std::string_view password,
              std::string_view salt,
              std::uint32_t iterations);

// Legacy M11.2 derivation: zero-pad/truncate password to 32 bytes.
// Retained for tables encrypted with header version 0xC3.
std::array<std::uint8_t, 32>
derive_legacy_encryption_key(std::string_view password);

// Current derivation for new encryptions (header version 0xC4).
std::array<std::uint8_t, 32>
derive_encryption_key(std::string_view password);

}  // namespace openads::engine