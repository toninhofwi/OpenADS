#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openads::engine {

struct Sha256Digest : std::array<std::uint8_t, 32> {};

Sha256Digest sha256(std::string_view data);
Sha256Digest sha256(const std::uint8_t* data, std::size_t len);
Sha256Digest hmac_sha256(std::string_view key, std::string_view message);

}  // namespace openads::engine