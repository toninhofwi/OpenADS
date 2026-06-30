#include "abi/charset.h"

#include <algorithm>
#include <cstring>

namespace openads::abi {

std::string to_internal(const std::uint8_t* p, std::size_t n) {
    if (p == nullptr) return {};
    if (n == 0) {
        // ACE convention: NUL-terminated when length is 0.
        n = std::strlen(reinterpret_cast<const char*>(p));
    }
    return std::string(reinterpret_cast<const char*>(p), n);
}

void copy_to_caller(std::uint8_t* dst, std::uint16_t* dst_len_inout,
                    const std::string& src) noexcept {
    if (dst == nullptr || dst_len_inout == nullptr) return;
    std::uint16_t cap = *dst_len_inout;
    std::uint16_t n   = static_cast<std::uint16_t>(
        std::min<std::size_t>(src.size(), cap == 0 ? 0 : cap - 1));
    std::memcpy(dst, src.data(), n);
    if (cap > 0) dst[n] = '\0';
    *dst_len_inout = n;
}

void copy_to_caller(std::uint8_t* dst, std::uint32_t* dst_len_inout,
                    const std::string& src) noexcept {
    if (dst == nullptr || dst_len_inout == nullptr) return;
    std::uint32_t cap = *dst_len_inout;
    std::uint32_t n   = static_cast<std::uint32_t>(
        std::min<std::size_t>(src.size(), cap == 0 ? 0u : cap - 1u));
    if (n > 0) std::memcpy(dst, src.data(), n);
    if (cap > 0) dst[n] = '\0';
    *dst_len_inout = n;
}

std::string utf16le_to_utf8(const std::uint16_t* in, std::size_t units) {
    std::string out;
    if (in == nullptr || units == 0) return out;
    out.reserve(units);  // ASCII-heavy lower bound
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // High surrogate; pair with following low surrogate.
            if (i + 1 < units) {
                std::uint32_t lo = in[i + 1];
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000u +
                         ((cp - 0xD800u) << 10) +
                          (lo - 0xDC00u);
                    ++i;
                } else {
                    cp = 0xFFFD;  // unpaired high surrogate
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;  // stray low surrogate
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

std::vector<std::uint16_t>
utf8_to_utf16le(const std::string& utf8) {
    std::vector<std::uint16_t> out;
    out.reserve(utf8.size());
    auto byte_at = [&](std::size_t i) -> unsigned char {
        return static_cast<unsigned char>(utf8[i]);
    };
    std::size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = byte_at(i);
        std::uint32_t cp = 0xFFFD;
        std::size_t adv = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0u) == 0xC0u && i + 1 < utf8.size() &&
                   (byte_at(i + 1) & 0xC0u) == 0x80u) {
            cp = (static_cast<std::uint32_t>(c & 0x1Fu) << 6) |
                  static_cast<std::uint32_t>(byte_at(i + 1) & 0x3Fu);
            adv = 2;
            if (cp < 0x80) cp = 0xFFFD;  // overlong
        } else if ((c & 0xF0u) == 0xE0u && i + 2 < utf8.size() &&
                   (byte_at(i + 1) & 0xC0u) == 0x80u &&
                   (byte_at(i + 2) & 0xC0u) == 0x80u) {
            cp = (static_cast<std::uint32_t>(c & 0x0Fu) << 12) |
                 (static_cast<std::uint32_t>(byte_at(i + 1) & 0x3Fu) << 6) |
                  static_cast<std::uint32_t>(byte_at(i + 2) & 0x3Fu);
            adv = 3;
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        } else if ((c & 0xF8u) == 0xF0u && i + 3 < utf8.size() &&
                   (byte_at(i + 1) & 0xC0u) == 0x80u &&
                   (byte_at(i + 2) & 0xC0u) == 0x80u &&
                   (byte_at(i + 3) & 0xC0u) == 0x80u) {
            cp = (static_cast<std::uint32_t>(c & 0x07u) << 18) |
                 (static_cast<std::uint32_t>(byte_at(i + 1) & 0x3Fu) << 12) |
                 (static_cast<std::uint32_t>(byte_at(i + 2) & 0x3Fu) << 6) |
                  static_cast<std::uint32_t>(byte_at(i + 3) & 0x3Fu);
            adv = 4;
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
        }
        if (cp < 0x10000) {
            out.push_back(static_cast<std::uint16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<std::uint16_t>(0xD800u | (cp >> 10)));
            out.push_back(static_cast<std::uint16_t>(0xDC00u | (cp & 0x3FFu)));
        }
        i += adv;
    }
    return out;
}

bool assign_u16(std::uint16_t* out, std::size_t n) noexcept {
    if (out == nullptr || n > 0xFFFFu) return false;
    *out = static_cast<std::uint16_t>(n);
    return true;
}

std::uint16_t clamp_u16(std::size_t n) noexcept {
    return static_cast<std::uint16_t>(std::min<std::size_t>(n, 0xFFFFu));
}

} // namespace openads::abi
