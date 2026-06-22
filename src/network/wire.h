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
    CloseTableAck      = 0x23,
    ExecuteSQL         = 0x30,
    ExecuteSQLAck      = 0x31,
    Fetch              = 0x32,
    FetchAck           = 0x33,
    // M12.4 — remote table navigation + read.
    GotoTop            = 0x40,
    GotoTopAck         = 0x41,
    Skip               = 0x42,
    SkipAck            = 0x43,
    GetField           = 0x44,
    GetFieldAck        = 0x45,
    GetRecordCount     = 0x46,
    GetRecordCountAck  = 0x47,
    AtEOF              = 0x48,
    AtEOFAck           = 0x49,
    // M12.14 — remote field metadata + extended cursor state.
    // DescribeTable returns the full per-column schema in one
    // round-trip so rddads' adsOpen path doesn't need 5 × num_fields
    // hops to populate AdsGetFieldName/Type/Length/Decimals.
    DescribeTable      = 0x4A,
    DescribeTableAck   = 0x4B,
    AtBOF              = 0x4C,
    AtBOFAck           = 0x4D,
    GetRecordNum       = 0x4E,
    GetRecordNumAck    = 0x4F,
    IsRecordDeleted    = 0x62,
    IsRecordDeletedAck = 0x63,
    GotoBottom         = 0x64,
    GotoBottomAck      = 0x65,
    // M12.15 — remote info / lock / maintenance / AOF.
    IsFound            = 0x66,
    IsFoundAck         = 0x67,
    RefreshRecord      = 0x68,
    RefreshRecordAck   = 0x69,
    GetTableType       = 0x6A,
    GetTableTypeAck    = 0x6B,
    GetRecordLength    = 0x6C,
    GetRecordLengthAck = 0x6D,
    GetNumIndexes      = 0x6E,
    GetNumIndexesAck   = 0x6F,
    GetLastAutoinc     = 0x70,
    GetLastAutoincAck  = 0x71,
    LockRecord         = 0x72,
    LockRecordAck      = 0x73,
    UnlockRecord       = 0x74,
    UnlockRecordAck    = 0x75,
    LockTable          = 0x76,
    LockTableAck       = 0x77,
    UnlockTable        = 0x78,
    UnlockTableAck     = 0x79,
    PackTable          = 0x7A,
    PackTableAck       = 0x7B,
    ZapTable           = 0x7C,
    ZapTableAck        = 0x7D,
    FlushFileBuffers   = 0x7E,
    FlushFileBuffersAck= 0x7F,
    CloseAllIndexes    = 0x80,
    CloseAllIndexesAck = 0x81,
    SetAOF             = 0x82,
    SetAOFAck          = 0x83,
    ClearAOFRemote     = 0x84,
    ClearAOFRemoteAck  = 0x85,
    GetAOFOptLevel     = 0x86,
    GetAOFOptLevelAck  = 0x87,
    // M12.16 — remote index handle subsystem.
    OpenIndex          = 0x88,
    OpenIndexAck       = 0x89,
    CloseIndex         = 0x8A,
    CloseIndexAck      = 0x8B,
    SetOrder           = 0x8C,
    SetOrderAck        = 0x8D,
    SetOrderByName     = 0x8E,
    SetOrderByNameAck  = 0x8F,
    Seek               = 0x90,
    SeekAck            = 0x91,
    SeekLast           = 0x92,
    SeekLastAck        = 0x93,
    CreateIndex        = 0x94,
    CreateIndexAck     = 0x95,
    SkipUnique         = 0x96,
    SkipUniqueAck      = 0x97,
    SetScope           = 0x98,
    SetScopeAck        = 0x99,
    ClearScope         = 0x9A,
    ClearScopeAck      = 0x9B,
    // M12.17 — single-frame whole-record read. xbrowse-style
    // viewers paint W cols × H rows = W*H FieldGet calls per
    // repaint; without this op every cell costs one TCP RTT
    // (~5-15 ms LAN), so a 20×12 grid stalls 1-4 s. With this op
    // RemoteTable caches the full row server-side and one
    // FetchCurrentRow RTT serves every subsequent FieldGet on
    // the same record.
    FetchCurrentRow    = 0x9C,
    FetchCurrentRowAck = 0x9D,
    // M12.6 — remote write surface.
    AppendBlank        = 0x50,
    AppendBlankAck     = 0x51,
    SetField           = 0x52,
    SetFieldAck        = 0x53,
    DeleteRecord       = 0x54,
    DeleteRecordAck    = 0x55,
    RecallRecord       = 0x56,
    RecallRecordAck    = 0x57,
    GotoRecord         = 0x58,
    GotoRecordAck      = 0x59,
    FlushTable         = 0x5A,
    FlushTableAck      = 0x5B,
    // M12.8 — remote index ops (CREATE INDEX is already covered by
    // M12.7's ExecuteSQL `CREATE INDEX` DDL path; Reindex isn't in
    // SQL grammar so it needs a dedicated opcode).
    Reindex            = 0x60,
    ReindexAck         = 0x61,
    // M12.24 — remote AdsGetLastTableUpdate. Reply payload is the
    // DBF header date packed big-endian-ish into 4 bytes:
    // (year << 16) | (month << 8) | day.
    GetLastTableUpdate    = 0x9E,
    GetLastTableUpdateAck = 0x9F,

    // M9.25 — management telemetry channel.
    MgConnect          = 0xA0,
    MgConnectAck       = 0xA1,
    MgRequest          = 0xA2,
    MgReplyAck         = 0xA3,

    Error              = 0xFF,
};

// Inbound cap — symmetric with encode_frame's outbound check; prevents
// multi-gigabyte resize on a malicious 4-byte length prefix.
inline constexpr std::uint32_t kMaxFramePayload = 16u * 1024u * 1024u;

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
