#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openads::network {

// M12.1 — Phase 2 wire-protocol skeleton for the TCP server.
//
// Frame layout (big-endian length, fixed-size header):
//
//   bytes 0..3 : payload length (uint32 BE, excludes header)
//   byte  4    : opcode
//   bytes 5..N : payload
//
// Total frame size = 5 + payload_length. The opcode space is
// reserved here even though the Phase 2 server is not yet wired
// up — apps and tests can already round-trip frames through
// encode_frame / decode_frame.

enum class Opcode : std::uint8_t {
    Hello              = 0x01,
    HelloAck           = 0x02,
    Connect            = 0x10,
    ConnectAck         = 0x11,
    Disconnect         = 0x12,
    OpenTable          = 0x20,
    OpenTableAck       = 0x21,
    CloseTable         = 0x22,
    ExecuteSQL         = 0x30,
    ExecuteSQLAck      = 0x31,
    Fetch              = 0x32,
    FetchAck           = 0x33,
    Error              = 0xFF,
};

struct Frame {
    Opcode                    opcode = Opcode::Hello;
    std::vector<std::uint8_t> payload;
};

// Encode `f` to a flat byte buffer ready to send over TCP.
util::Result<std::vector<std::uint8_t>> encode_frame(const Frame& f);

// Decode `f` from `buf` (must contain at least one full frame).
// Returns the decoded frame plus the number of bytes consumed via
// the optional `consumed` out-param.
util::Result<Frame> decode_frame(const std::uint8_t* buf,
                                  std::size_t size,
                                  std::size_t* consumed = nullptr);

} // namespace openads::network
