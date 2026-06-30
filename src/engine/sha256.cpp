#include "engine/sha256.h"

#include <cstring>

namespace openads::engine {

namespace {

constexpr std::uint32_t kInit[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

void sha256_compress(std::uint32_t state[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                                 (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                                 (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + kRound[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

Sha256Digest sha256_impl(const std::uint8_t* data, std::size_t len) {
    std::uint32_t state[8];
    std::memcpy(state, kInit, sizeof(state));

    std::uint64_t total_bits = static_cast<std::uint64_t>(len) * 8u;
    std::size_t offset = 0;
    while (len - offset >= 64) {
        sha256_compress(state, data + offset);
        offset += 64;
    }

    std::uint8_t block[64]{};
    const std::size_t rem = len - offset;
    if (rem > 0) {
        std::memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80u;
    if (rem < 56) {
        for (std::size_t i = 0; i < 8; ++i) {
            block[56 + i] = static_cast<std::uint8_t>(
                (total_bits >> ((7 - i) * 8)) & 0xFFu);
        }
        sha256_compress(state, block);
    } else {
        sha256_compress(state, block);
        std::memset(block, 0, 64);
        for (std::size_t i = 0; i < 8; ++i) {
            block[56 + i] = static_cast<std::uint8_t>(
                (total_bits >> ((7 - i) * 8)) & 0xFFu);
        }
        sha256_compress(state, block);
    }

    Sha256Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        const std::size_t b = i * 4;
        out[b]     = static_cast<std::uint8_t>((state[i] >> 24) & 0xFFu);
        out[b + 1] = static_cast<std::uint8_t>((state[i] >> 16) & 0xFFu);
        out[b + 2] = static_cast<std::uint8_t>((state[i] >> 8) & 0xFFu);
        out[b + 3] = static_cast<std::uint8_t>(state[i] & 0xFFu);
    }
    return out;
}

}  // namespace

Sha256Digest sha256(std::string_view data) {
    return sha256_impl(reinterpret_cast<const std::uint8_t*>(data.data()),
                       data.size());
}

Sha256Digest sha256(const std::uint8_t* data, std::size_t len) {
    return sha256_impl(data, len);
}

Sha256Digest hmac_sha256(std::string_view key, std::string_view message) {
    std::uint8_t k[64]{};
    if (key.size() > 64) {
        const Sha256Digest hk = sha256(key);
        std::memcpy(k, hk.data(), hk.size());
    } else if (!key.empty()) {
        std::memcpy(k, key.data(), key.size());
    }

    std::uint8_t ipad[64];
    std::uint8_t opad[64];
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = static_cast<std::uint8_t>(k[i] ^ 0x36u);
        opad[i] = static_cast<std::uint8_t>(k[i] ^ 0x5Cu);
    }

    std::uint8_t inner[64 + 256]{};
    std::memcpy(inner, ipad, 64);
    if (!message.empty()) {
        std::memcpy(inner + 64, message.data(), message.size());
    }
    const Sha256Digest inner_hash =
        sha256(inner, 64 + message.size());

    std::uint8_t outer[64 + 32]{};
    std::memcpy(outer, opad, 64);
    std::memcpy(outer + 64, inner_hash.data(), inner_hash.size());
    return sha256(outer, 64 + inner_hash.size());
}

}  // namespace openads::engine