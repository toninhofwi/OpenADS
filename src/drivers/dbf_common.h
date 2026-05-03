#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers {

enum class DbfFamily {
    Clipper,    // 0x03 (no memo) / 0x83 (with memo)
    Vfp,        // 0x30 / 0x31
    Unknown
};

struct DbfHeader {
    std::uint8_t  version          = 0;
    std::uint16_t last_update_year = 0;
    std::uint8_t  last_update_month = 0;
    std::uint8_t  last_update_day  = 0;
    std::uint32_t record_count     = 0;
    std::uint16_t header_length    = 0;
    std::uint16_t record_length    = 0;
    DbfFamily     family           = DbfFamily::Unknown;
};

util::Result<DbfHeader> parse_dbf_header(const std::uint8_t* data,
                                         std::size_t size);

enum class DbfFieldType {
    Character,
    Numeric,
    Float,
    Date,
    DateTime,
    Logical,
    Memo,
    Integer,    // VFP I (4-byte int)
    Currency,   // VFP Y
    Double,     // VFP B
    Unknown
};

struct DbfField {
    std::string   name;
    DbfFieldType  type          = DbfFieldType::Unknown;
    char          raw_type      = '\0';
    std::uint8_t  length        = 0;
    std::uint8_t  decimals      = 0;
    std::uint16_t record_offset = 0; // includes the leading deletion byte
};

util::Result<std::vector<DbfField>>
parse_dbf_fields(const std::uint8_t* data, std::size_t size);

} // namespace openads::drivers
