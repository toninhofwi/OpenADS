#include "engine/codepage.h"

#include <array>
#include <unordered_map>

namespace openads::engine {

namespace {

// CP437 high half (0x80..0xFF) → Unicode codepoints.
// Source: IBM Code Page 437 specification (public).
constexpr std::array<std::uint32_t, 128> kCp437Upper = {
    /*80*/ 0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    /*88*/ 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    /*90*/ 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    /*98*/ 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    /*A0*/ 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    /*A8*/ 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    /*B0*/ 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    /*B8*/ 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    /*C0*/ 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    /*C8*/ 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    /*D0*/ 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    /*D8*/ 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    /*E0*/ 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    /*E8*/ 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    /*F0*/ 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    /*F8*/ 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

void encode_utf8(std::uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 |  (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >>  6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 |  (cp        & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 |  (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >>  6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 |  (cp        & 0x3F)));
    }
}

const std::unordered_map<std::uint32_t, std::uint8_t>& reverse_table() {
    static const auto m = []{
        std::unordered_map<std::uint32_t, std::uint8_t> r;
        for (std::size_t i = 0; i < kCp437Upper.size(); ++i) {
            r.emplace(kCp437Upper[i],
                      static_cast<std::uint8_t>(0x80 + i));
        }
        return r;
    }();
    return m;
}

} // namespace

std::string cp437_to_utf8(const std::uint8_t* in, std::size_t n) {
    std::string out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t b = in[i];
        if (b < 0x80) out.push_back(static_cast<char>(b));
        else encode_utf8(kCp437Upper[b - 0x80], out);
    }
    return out;
}

std::string utf8_to_cp437(const char* in, std::size_t n) {
    const auto& rev = reverse_table();
    std::string out;
    std::size_t i = 0;
    while (i < n) {
        std::uint8_t b = static_cast<std::uint8_t>(in[i]);
        std::uint32_t cp = 0;
        std::size_t consumed = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < n) {
            cp = (static_cast<std::uint32_t>(b & 0x1F) << 6) |
                 (static_cast<std::uint8_t>(in[i + 1]) & 0x3F);
            consumed = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < n) {
            cp = (static_cast<std::uint32_t>(b & 0x0F) << 12) |
                 ((static_cast<std::uint8_t>(in[i + 1]) & 0x3F) << 6) |
                 ( static_cast<std::uint8_t>(in[i + 2]) & 0x3F);
            consumed = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < n) {
            cp = (static_cast<std::uint32_t>(b & 0x07) << 18) |
                 ((static_cast<std::uint8_t>(in[i + 1]) & 0x3F) << 12) |
                 ((static_cast<std::uint8_t>(in[i + 2]) & 0x3F) <<  6) |
                 ( static_cast<std::uint8_t>(in[i + 3]) & 0x3F);
            consumed = 4;
        } else {
            cp = '?';
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else {
            auto it = rev.find(cp);
            out.push_back(it != rev.end()
                ? static_cast<char>(it->second)
                : '?');
        }
        i += consumed;
    }
    return out;
}

} // namespace openads::engine
