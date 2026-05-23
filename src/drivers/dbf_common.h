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
    // M11.2 — OpenADS-only encrypted DBF (header byte 0xC3). Not
    // byte-compatible with SAP ADS encrypted .adt files.
    bool          encrypted        = false;
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
    Varchar,    // VFP V — variable-length character; M11.1
    Varbinary,  // VFP Q — variable-length binary; M11.1
    // ADT-native types (M4)
    ShortInt,       // ADT type 12: 2-byte int16 LE
    Binary,         // ADT type  6: binary/image blob (9-byte in-record ref)
    CiCharacter,    // ADT type 20: case-insensitive char (same wire encoding as Character)
    AutoInc,        // ADT type 15: 4-byte uint32 auto-increment, read-only via ADS
    Time,           // ADT type 13: 4-byte uint32 milliseconds since midnight
    AdtDate,        // ADT type  3: 4-byte uint32 Julian Day Number
    AdtTimestamp,   // ADT type 14: 4-byte JDN + 4-byte ms-since-midnight (8 bytes total)
    Unknown
};

struct DbfField {
    std::string   name;
    DbfFieldType  type          = DbfFieldType::Unknown;
    char          raw_type      = '\0';
    std::uint16_t length        = 0;  // widened to uint16 for ADT fields up to 65535 bytes
    std::uint8_t  decimals      = 0;
    std::uint16_t record_offset = 0; // includes the leading deletion byte
    // VFP autoinc (M10.11). `autoinc` is true when the field-descriptor's
    // flags byte at offset 18 has bit 0 set; `autoinc_next` mirrors the
    // 4-byte LE counter at offset 19 + 22, and `autoinc_step` is the
    // single-byte step at offset 23.
    bool          autoinc       = false;
    std::uint32_t autoinc_next  = 0;
    std::uint8_t  autoinc_step  = 1;
    // M11.6 — VFP NULL bitmap. `nullable` mirrors flags-byte bit 1
    // on the field descriptor; nullable fields claim a bit in the
    // table-scope `_NullFlags` system field, indexed by the field's
    // ordinal among nullable fields. `null_bit` is that ordinal.
    bool          nullable      = false;
    std::uint16_t null_bit      = 0;
};

util::Result<std::vector<DbfField>>
parse_dbf_fields(const std::uint8_t* data, std::size_t size);

struct DbfFieldValue {
    std::string  as_string;
    double       as_double = 0.0;
    bool         as_bool   = false;
    bool         is_null   = false;
};

util::Result<DbfFieldValue> decode_field(const DbfField& field,
                                         const std::uint8_t* record_buf,
                                         std::size_t record_size);

bool record_is_deleted(const std::uint8_t* record_buf,
                       std::size_t record_size) noexcept;

std::vector<std::uint8_t> make_empty_record(std::uint16_t record_length);

util::Result<void> encode_field_string (const DbfField& f,
                                        std::uint8_t* rec, std::size_t rec_size,
                                        const std::string& value);
util::Result<void> encode_field_double (const DbfField& f,
                                        std::uint8_t* rec, std::size_t rec_size,
                                        double value);
util::Result<void> encode_field_logical(const DbfField& f,
                                        std::uint8_t* rec, std::size_t rec_size,
                                        bool value);

void set_record_deleted(std::uint8_t* rec, std::size_t rec_size,
                        bool deleted) noexcept;

} // namespace openads::drivers
