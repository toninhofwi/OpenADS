#include "drivers/dbf_common.h"

namespace openads::drivers {

namespace {

DbfFamily classify(std::uint8_t version) {
    switch (version) {
        case 0x03: case 0x83:
            return DbfFamily::Clipper;
        case 0x30: case 0x31: case 0x32:
            return DbfFamily::Vfp;
        default:
            return DbfFamily::Unknown;
    }
}

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) << 8)  |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

util::Result<DbfHeader> parse_dbf_header(const std::uint8_t* data,
                                         std::size_t size) {
    if (size < 32) {
        return util::Error{5103, 0, "DBF header smaller than 32 bytes", ""};
    }
    DbfHeader h;
    h.version = data[0];
    // YY in DBF header is years since 1900. Apply that base.
    h.last_update_year  = static_cast<std::uint16_t>(1900 + data[1]);
    h.last_update_month = data[2];
    h.last_update_day   = data[3];
    h.record_count      = read_u32_le(data + 4);
    h.header_length     = read_u16_le(data + 8);
    h.record_length     = read_u16_le(data + 10);
    h.family            = classify(h.version);
    return h;
}

namespace {

DbfFieldType classify_field(char raw) {
    switch (raw) {
        case 'C': return DbfFieldType::Character;
        case 'N': return DbfFieldType::Numeric;
        case 'F': return DbfFieldType::Float;
        case 'D': return DbfFieldType::Date;
        case 'T': return DbfFieldType::DateTime;
        case 'L': return DbfFieldType::Logical;
        case 'M': return DbfFieldType::Memo;
        case 'I': return DbfFieldType::Integer;
        case 'Y': return DbfFieldType::Currency;
        case 'B': return DbfFieldType::Double;
        default:  return DbfFieldType::Unknown;
    }
}

} // namespace

util::Result<std::vector<DbfField>>
parse_dbf_fields(const std::uint8_t* data, std::size_t size) {
    std::vector<DbfField> out;
    std::uint16_t offset = 1; // skip leading deletion byte

    std::size_t pos = 0;
    while (pos + 32 <= size) {
        if (data[pos] == 0x0D) break;
        DbfField f;
        const char* raw_name = reinterpret_cast<const char*>(data + pos);
        std::size_t name_len = 0;
        while (name_len < 11 && raw_name[name_len] != '\0') ++name_len;
        f.name.assign(raw_name, name_len);
        f.raw_type      = static_cast<char>(data[pos + 11]);
        f.type          = classify_field(f.raw_type);
        f.length        = data[pos + 16];
        f.decimals      = data[pos + 17];
        f.record_offset = offset;
        offset = static_cast<std::uint16_t>(offset + f.length);
        out.push_back(std::move(f));
        pos += 32;
    }
    return out;
}

} // namespace openads::drivers
