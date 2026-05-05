#include "network/wire.h"

#include <cstring>

namespace openads::network {

util::Result<std::vector<std::uint8_t>> encode_frame(const Frame& f) {
    if (f.payload.size() > 0xFFFFFFFFu) {
        return util::Error{5000, 0, "frame payload too large", ""};
    }
    std::vector<std::uint8_t> out;
    out.reserve(5 + f.payload.size());
    std::uint32_t n = static_cast<std::uint32_t>(f.payload.size());
    out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(f.opcode));
    if (n > 0) {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }
    return out;
}

util::Result<Frame> decode_frame(const std::uint8_t* buf,
                                  std::size_t size,
                                  std::size_t* consumed) {
    if (size < 5) {
        return util::Error{5000, 0, "frame buffer shorter than header", ""};
    }
    std::uint32_t n =
        (static_cast<std::uint32_t>(buf[0]) << 24) |
        (static_cast<std::uint32_t>(buf[1]) << 16) |
        (static_cast<std::uint32_t>(buf[2]) <<  8) |
         static_cast<std::uint32_t>(buf[3]);
    if (size < 5 + static_cast<std::size_t>(n)) {
        return util::Error{5000, 0, "frame buffer truncated", ""};
    }
    Frame f;
    f.opcode = static_cast<Opcode>(buf[4]);
    if (n > 0) {
        f.payload.assign(buf + 5, buf + 5 + n);
    }
    if (consumed) *consumed = 5 + n;
    return f;
}

} // namespace openads::network
