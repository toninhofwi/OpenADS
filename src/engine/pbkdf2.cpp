#include "engine/pbkdf2.h"

#include "engine/sha256.h"

#include <algorithm>
#include <cstring>

namespace openads::engine {

namespace {

constexpr std::string_view kOpenAdsSalt = "OpenADS-Enc-v1";
constexpr std::uint32_t    kIterations  = 100000u;

}  // namespace

std::array<std::uint8_t, 32>
pbkdf2_sha256(std::string_view password,
              std::string_view salt,
              std::uint32_t iterations) {
    std::array<std::uint8_t, 32> out{};
    if (iterations == 0) return out;

    const std::size_t blocks =
        (out.size() + Sha256Digest{}.size() - 1) / Sha256Digest{}.size();
    std::size_t produced = 0;

    for (std::size_t block = 1; block <= blocks; ++block) {
        const std::uint32_t block_index = static_cast<std::uint32_t>(block);
        std::uint8_t be[4] = {
            static_cast<std::uint8_t>((block_index >> 24) & 0xFFu),
            static_cast<std::uint8_t>((block_index >> 16) & 0xFFu),
            static_cast<std::uint8_t>((block_index >> 8) & 0xFFu),
            static_cast<std::uint8_t>(block_index & 0xFFu),
        };

        std::string u_input;
        u_input.reserve(salt.size() + 4);
        u_input.append(salt.data(), salt.size());
        u_input.append(reinterpret_cast<const char*>(be), 4);

        Sha256Digest u = hmac_sha256(password, u_input);
        Sha256Digest t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = hmac_sha256(password,
                            std::string_view(
                                reinterpret_cast<const char*>(u.data()),
                                u.size()));
            for (std::size_t j = 0; j < t.size(); ++j) {
                t[j] ^= u[j];
            }
        }

        const std::size_t copy =
            std::min<std::size_t>(t.size(), out.size() - produced);
        std::memcpy(out.data() + produced, t.data(), copy);
        produced += copy;
    }
    return out;
}

std::array<std::uint8_t, 32>
derive_legacy_encryption_key(std::string_view password) {
    std::array<std::uint8_t, 32> key{};
    const std::size_t n =
        std::min<std::size_t>(password.size(), key.size());
    if (n > 0) {
        std::memcpy(key.data(), password.data(), n);
    }
    return key;
}

std::array<std::uint8_t, 32>
derive_encryption_key(std::string_view password) {
    return pbkdf2_sha256(password, kOpenAdsSalt, kIterations);
}

}  // namespace openads::engine