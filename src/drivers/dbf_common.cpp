#include "drivers/dbf_common.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace openads::drivers {

namespace {

DbfFamily classify(std::uint8_t version) {
    switch (version) {
        case 0x03: case 0x83:
        case 0xC3:                          // M11.2 — encrypted variant
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
    h.encrypted         = (h.version == 0xC3);
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
        case 'V': return DbfFieldType::Varchar;     // M11.1
        case 'Q': return DbfFieldType::Varbinary;   // M11.1
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
        // VFP autoinc descriptor bytes (M10.11). Non-VFP DBFs have
        // these slots zeroed so the read is harmless.
        std::uint8_t flags = data[pos + 18];
        f.autoinc      = (flags & 0x0Cu) != 0;   // VFP marks autoinc
                                                 // with bits 2+3 set
        // M11.6 — VFP NULL flag = bit 1 (0x02) of the flags byte.
        // Each nullable field gets a position in the table-wide
        // _NullFlags field; the position is assigned in declaration
        // order during parse_dbf_fields.
        f.nullable = (flags & 0x02u) != 0;
        f.autoinc_next =  static_cast<std::uint32_t>(data[pos + 19])        |
                         (static_cast<std::uint32_t>(data[pos + 20]) <<  8) |
                         (static_cast<std::uint32_t>(data[pos + 21]) << 16) |
                         (static_cast<std::uint32_t>(data[pos + 22]) << 24);
        f.autoinc_step = data[pos + 23];
        if (f.autoinc_step == 0) f.autoinc_step = 1;
        offset = static_cast<std::uint16_t>(offset + f.length);
        out.push_back(std::move(f));
        pos += 32;
    }
    // M11.6 — assign null-bit positions to every nullable field in
    // declaration order. Apps that touch _NullFlags directly read
    // these bits via `Table::is_field_null`.
    std::uint16_t nb = 0;
    for (auto& fd : out) {
        if (fd.nullable) fd.null_bit = nb++;
    }
    return out;
}

namespace {

std::string make_string(const std::uint8_t* p, std::size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

double parse_numeric(const std::uint8_t* p, std::size_t n) {
    char tmp[64];
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    std::memcpy(tmp, p, n);
    tmp[n] = '\0';
    char* end = nullptr;
    return std::strtod(tmp, &end);
}

// VFP I (Integer) — 4-byte little-endian signed int32.
std::int32_t read_i32_le(const std::uint8_t* p) {
    std::uint32_t u =
         static_cast<std::uint32_t>(p[0])        |
        (static_cast<std::uint32_t>(p[1]) <<  8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
    return static_cast<std::int32_t>(u);
}

// VFP Y (Currency) — 8-byte little-endian signed int64 of money * 10000.
std::int64_t read_i64_le(const std::uint8_t* p) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return static_cast<std::int64_t>(u);
}

// VFP B (Double) — 8-byte IEEE 754 little-endian double.
double read_f64_le(const std::uint8_t* p) {
    std::uint64_t u = static_cast<std::uint64_t>(read_i64_le(p));
    double out;
    std::memcpy(&out, &u, sizeof(out));
    return out;
}

void write_i32_le(std::uint8_t* p, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    p[0] = static_cast<std::uint8_t>( u        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((u >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((u >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((u >> 24) & 0xFFu);
}

void write_i64_le(std::uint8_t* p, std::int64_t v) {
    auto u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu);
    }
}

void write_f64_le(std::uint8_t* p, double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    write_i64_le(p, static_cast<std::int64_t>(u));
}

} // namespace

util::Result<DbfFieldValue> decode_field(const DbfField& field,
                                         const std::uint8_t* record_buf,
                                         std::size_t record_size) {
    DbfFieldValue v;
    if (static_cast<std::size_t>(field.record_offset) +
        static_cast<std::size_t>(field.length) > record_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    const std::uint8_t* p = record_buf + field.record_offset;

    switch (field.type) {
        case DbfFieldType::Character:
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::Numeric:
        case DbfFieldType::Float:
            v.as_double = parse_numeric(p, field.length);
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::Date:
        case DbfFieldType::DateTime:
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::Logical: {
            char c = static_cast<char>(p[0]);
            v.as_bool   = (c == 'T' || c == 't' || c == 'Y' || c == 'y');
            v.as_string = std::string(1, c);
            break;
        }

        case DbfFieldType::Memo:
            // M1 deliberately does not load memo blocks; they land in M4.
            v.as_string.clear();
            break;

        case DbfFieldType::Integer: {
            // VFP I — 4-byte little-endian signed int32.
            if (field.length < 4) {
                v.as_string = make_string(p, field.length);
                break;
            }
            std::int32_t n = read_i32_le(p);
            v.as_double = static_cast<double>(n);
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%d",
                          static_cast<int>(n));
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Currency: {
            // VFP Y — 8-byte little-endian signed int64, money * 10000.
            if (field.length < 8) {
                v.as_string = make_string(p, field.length);
                break;
            }
            std::int64_t raw = read_i64_le(p);
            double money = static_cast<double>(raw) / 10000.0;
            v.as_double = money;
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.4f", money);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Double: {
            // VFP B — 8-byte IEEE 754 little-endian double.
            if (field.length < 8) {
                v.as_string = make_string(p, field.length);
                break;
            }
            v.as_double = read_f64_le(p);
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.*f",
                          static_cast<int>(field.decimals),
                          v.as_double);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Varchar: {
            // VFP V — fixed-width slot trimmed of trailing NULs. Apps
            // see only the meaningful prefix; everything after the
            // first 0x00 is padding.
            std::size_t actual = field.length;
            while (actual > 0 && p[actual - 1] == 0x00) --actual;
            v.as_string = std::string(
                reinterpret_cast<const char*>(p), actual);
            break;
        }
        case DbfFieldType::Varbinary: {
            // VFP Q — raw bytes; trailing 0x00 also trimmed for
            // string view (binary callers should use raw_record_raw
            // for byte-exact access).
            std::size_t actual = field.length;
            while (actual > 0 && p[actual - 1] == 0x00) --actual;
            v.as_string = std::string(
                reinterpret_cast<const char*>(p), actual);
            break;
        }
        case DbfFieldType::Unknown:
            v.as_string = make_string(p, field.length);
            break;
    }
    return v;
}

bool record_is_deleted(const std::uint8_t* record_buf,
                       std::size_t record_size) noexcept {
    if (record_size == 0) return false;
    return record_buf[0] == '*';
}

std::vector<std::uint8_t> make_empty_record(std::uint16_t record_length) {
    return std::vector<std::uint8_t>(record_length, ' ');
}

util::Result<void> encode_field_string(const DbfField& f,
                                       std::uint8_t* rec, std::size_t rec_size,
                                       const std::string& value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    std::uint8_t* dst = rec + f.record_offset;
    std::size_t n = std::min<std::size_t>(value.size(), f.length);
    std::memcpy(dst, value.data(), n);
    // M11.1 — VFP V / Q pad the unused tail with NUL so callers can
    // recover the actual length on read; everything else (Character,
    // Numeric, Date, ...) keeps the legacy space-pad behavior.
    std::uint8_t pad =
        (f.type == DbfFieldType::Varchar ||
         f.type == DbfFieldType::Varbinary)
            ? 0x00 : ' ';
    for (std::size_t i = n; i < f.length; ++i) dst[i] = pad;
    return {};
}

util::Result<void> encode_field_double(const DbfField& f,
                                       std::uint8_t* rec, std::size_t rec_size,
                                       double value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    std::uint8_t* dst = rec + f.record_offset;

    // VFP binary types take a fixed-width little-endian payload, not
    // an ASCII representation; route them through the right packer
    // before falling back to the Clipper-style ASCII numeric encoding.
    switch (f.type) {
        case DbfFieldType::Integer:
            if (f.length >= 4) {
                write_i32_le(dst, static_cast<std::int32_t>(value));
                return {};
            }
            break;
        case DbfFieldType::Currency:
            if (f.length >= 8) {
                write_i64_le(dst,
                    static_cast<std::int64_t>(value * 10000.0));
                return {};
            }
            break;
        case DbfFieldType::Double:
            if (f.length >= 8) {
                write_f64_le(dst, value);
                return {};
            }
            break;
        default:
            break;
    }

    char tmp[64];
    int written = std::snprintf(tmp, sizeof(tmp), "%*.*f",
                                static_cast<int>(f.length),
                                static_cast<int>(f.decimals),
                                value);
    if (written < 0) {
        return util::Error{5000, 0, "snprintf failed encoding numeric", ""};
    }
    std::size_t n = static_cast<std::size_t>(written);
    if (n > f.length) n = f.length;
    std::memcpy(dst, tmp, n);
    for (std::size_t i = n; i < f.length; ++i) dst[i] = ' ';
    return {};
}

util::Result<void> encode_field_logical(const DbfField& f,
                                        std::uint8_t* rec, std::size_t rec_size,
                                        bool value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    rec[f.record_offset] = value ? 'T' : 'F';
    return {};
}

void set_record_deleted(std::uint8_t* rec, std::size_t rec_size,
                        bool deleted) noexcept {
    if (rec_size == 0) return;
    rec[0] = deleted ? '*' : ' ';
}

} // namespace openads::drivers
