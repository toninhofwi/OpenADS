#include "drivers/adi/adi_index.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace openads::drivers::adi {

namespace {

// ── Byte-level helpers ────────────────────────────────────────────────────────

std::uint16_t u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
}

std::uint32_t u32_le(const std::uint8_t* p) noexcept {
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) <<  8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t u32_be(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) <<  8)
         |  static_cast<std::uint32_t>(p[3]);
}

void set_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void set_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >>  8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

void set_u32_be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >>  8);
    p[3] = static_cast<std::uint8_t>(v);
}

// Dense entry recno from a raw byte buffer (not a Page), given 0-based idx.
std::uint32_t dense_recno_from_buf(const std::uint8_t* base, std::uint32_t idx,
                                   std::uint32_t esz) noexcept {
    const std::uint8_t* e = base + idx * esz;
    if (esz >= 3)
        return static_cast<std::uint32_t>(e[0]) | (static_cast<std::uint32_t>(e[1]) << 8);
    return e[0];
}

platform::OpenMode map_open_mode(IndexOpenMode m) noexcept {
    if (m == IndexOpenMode::ReadOnly) return platform::OpenMode::ReadOnly;
    return platform::OpenMode::OpenExisting;
}

// Derive .adt path from .adi path
std::string adt_path_for(const std::string& adi_path) {
    namespace fs = std::filesystem;
    fs::path p(adi_path);
    p.replace_extension(".adt");
    return p.string();
}

// ── ADI page-header helpers ───────────────────────────────────────────────────

std::uint16_t page_level(const std::uint8_t* pg) noexcept { return u16_le(pg);   }
std::uint16_t page_count(const std::uint8_t* pg) noexcept { return u16_le(pg+2); }
std::uint32_t page_lsib (const std::uint8_t* pg) noexcept { return u32_le(pg+4); }
std::uint32_t page_rsib (const std::uint8_t* pg) noexcept { return u32_le(pg+8); }

// ── Numeric branch entry (level 0 and 1): starts at offset 12 ───────────────
// Format: key[8](BE sign-flipped float64) + cum[4](BE uint32) + page_no[4](BE uint32)

std::uint32_t tree_entry_page(const std::uint8_t* pg, int idx) noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * ADI_TREE_ENTRY_SIZE;
    return u32_be(e + 12);
}

const std::uint8_t* tree_entry_key(const std::uint8_t* pg, int idx) noexcept {
    return pg + ADI_TREE_ENTRY_START
           + static_cast<std::uint32_t>(idx) * ADI_TREE_ENTRY_SIZE;
}

// ── Character branch entry (level 1 for char-key ADI) ────────────────────────
// Format: padded_key[key_padded_len] + cum[4 LE] + page[1]

std::uint32_t char_tree_entry_page(const std::uint8_t* pg, int idx,
                                   std::uint32_t entry_sz,
                                   std::uint32_t key_padded_len) noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * entry_sz;
    return static_cast<std::uint32_t>(e[key_padded_len + 4]);
}

// ── Dense-leaf entry: starts at offset 24 ────────────────────────────────────
// Format (entry_sz=3, wider key fields): recno[2 LE] + type_byte[1]
// Format (entry_sz=2, 1-byte key fields): recno[1] + key_flags[1]
// Confirmed by probe against SAP ACE ground truth (propertytransactions, 12331 rows).

std::uint32_t dense_entry_recno(const std::uint8_t* pg, int idx,
                                std::uint32_t entry_sz) noexcept {
    const std::uint8_t* e = pg + ADI_DENSE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * entry_sz;
    if (entry_sz >= 3)
        return static_cast<std::uint32_t>(e[0]) | (static_cast<std::uint32_t>(e[1]) << 8);
    return e[0];  // 2-byte entry: recno in byte 0, byte 1 is key-flags
}

// ── Key encoding ─────────────────────────────────────────────────────────────

// Pack a double into an 8-byte IEEE 754 total-order big-endian ADI key.
// Positive values: flip sign bit only (0x80).
// Negative values: flip all bits so they sort before positives and among
// themselves in the correct order (most-negative first).
std::string pack_double_key(double v) {
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);               // raw = IEEE754 LE on x86
    std::reverse(raw, raw + 8);            // LE → BE
    if (raw[0] & 0x80u) {
        for (auto& b : raw) b = ~b;        // negative: flip all bits
    } else {
        raw[0] ^= 0x80u;                   // non-negative: flip sign bit only
    }
    return std::string(reinterpret_cast<char*>(raw), 8);
}

// Encode an ADT field value to ADI key bytes, given ADT type and field data.
// For character types (CICHAR, CHAR): returns the raw field bytes (length bytes).
// For numeric types: returns an 8-byte IEEE 754 total-order BE key.
std::string encode_adt_key(std::uint16_t adt_type, const std::uint8_t* data,
                           std::uint16_t length) {
    // Character types: key is the raw field data (space-padded in ADT already)
    if (adt_type == ADT_TYPE_CICHAR || adt_type == ADT_TYPE_CHAR) {
        return std::string(reinterpret_cast<const char*>(data), length);
    }

    double val = 0.0;
    switch (adt_type) {
        case ADT_TYPE_DATE: {
            std::uint32_t jdn = u32_le(data);
            val = static_cast<double>(jdn);
            break;
        }
        case ADT_TYPE_AUTOINC:
        case ADT_TYPE_TIME: {
            std::uint32_t v = u32_le(data);
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_INTEGER: {
            std::int32_t v = static_cast<std::int32_t>(u32_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_SHORTINT: {
            std::int16_t v = static_cast<std::int16_t>(u16_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_DOUBLE:
        case ADT_TYPE_MONEY:
        case ADT_TYPE_MODTIME: {
            double v;
            std::memcpy(&v, data, 8);
            return pack_double_key(v);
        }
        case ADT_TYPE_TIMESTAMP: {
            std::uint64_t v = static_cast<std::uint64_t>(u32_le(data)) |
                              (static_cast<std::uint64_t>(u32_le(data+4)) << 32);
            val = static_cast<double>(v);
            break;
        }
        default:
            (void)length;
            return std::string(8, '\0');
    }
    return pack_double_key(val);
}

// ── ADT header parsing (minimal) ─────────────────────────────────────────────

struct AdtFieldDesc {
    std::string   name;
    std::uint16_t type;
    std::uint16_t offset;
    std::uint16_t length;
};

// Read the first 400+num_fields*200 bytes of the ADT file, return field list.
util::Result<std::vector<AdtFieldDesc>>
read_adt_fields(platform::File& f, std::uint32_t& hdr_len_out,
                                   std::uint32_t& rec_len_out) {
    // Minimal header read: first 400 bytes
    std::uint8_t buf[400];
    auto got = f.read_at(0, buf, sizeof(buf));
    if (!got || got.value() < sizeof(buf))
        return util::Error{6106, 0, "ADT header truncated", ""};

    std::uint32_t hdr_len = u32_le(buf + 32);
    std::uint32_t rec_len = u32_le(buf + 36);
    hdr_len_out = hdr_len;
    rec_len_out = rec_len;

    std::uint32_t num_fields = (hdr_len - 400) / 200;
    std::vector<std::uint8_t> fld_buf(num_fields * 200);
    got = f.read_at(400, fld_buf.data(), fld_buf.size());
    if (!got || got.value() < fld_buf.size())
        return util::Error{6106, 0, "ADT field descriptors truncated", ""};

    std::vector<AdtFieldDesc> fields;
    fields.reserve(num_fields);
    for (std::uint32_t i = 0; i < num_fields; ++i) {
        const std::uint8_t* d = fld_buf.data() + i * 200;
        AdtFieldDesc fd;
        // Name: null-terminated at bytes 0-127
        std::size_t nlen = 0;
        while (nlen < 128 && d[nlen]) ++nlen;
        fd.name   = std::string(reinterpret_cast<const char*>(d), nlen);
        fd.type   = u16_le(d + 129);
        fd.offset = u16_le(d + 131);
        fd.length = u16_le(d + 135);
        fields.push_back(std::move(fd));
    }
    return fields;
}

// ── F-marker scanning ─────────────────────────────────────────────────────────

// Read one 512-byte page from an already-open ADI file.
util::Result<AdiIndex::Page> read_one_page(platform::File& f,
                                           std::uint32_t page_no) {
    AdiIndex::Page pg{};
    auto got = f.read_at(static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
                         pg.data(), pg.size());
    if (!got) return got.error();
    if (got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "short ADI page read", ""};
    return pg;
}

// Parse an F-marker page and return ALL 1-based field numbers it encodes.
// Single-field example:  "F1\0..."   → [1]
// Compound-field example: "F2;F14\0" → [2, 14]
// Returns an empty vector for invalid/unrecognised markers.
std::vector<std::uint8_t> parse_fmarker_all(const AdiIndex::Page& pg) {
    if (pg[0] != 'F') return {};
    std::vector<std::uint8_t> result;
    std::size_t i = 1;  // skip leading 'F'
    while (i < ADI_PAGE_SIZE) {
        if (pg[i] < '1' || pg[i] > '9') break;
        std::uint32_t n = 0;
        while (i < ADI_PAGE_SIZE && pg[i] >= '0' && pg[i] <= '9')
            n = n * 10 + (pg[i++] - '0');
        if (n > 0 && n <= 255) result.push_back(static_cast<std::uint8_t>(n));
        // Compound separator: ';' followed by 'F'
        if (i + 1 < ADI_PAGE_SIZE && pg[i] == ';' && pg[i+1] == 'F')
            i += 2;
        else
            break;
    }
    return result;
}

// One entry in the tag directory scan result.
struct TagEntry {
    std::vector<std::uint8_t> fnums;  // 1-based field numbers (≥1 element)
    std::uint32_t             root_pg;
    bool                      unique = false;  // bit 0 of byte[14] in per-tag header page
};

// Scan tag directory (page 2) and return all tag entries.
util::Result<std::vector<TagEntry>>
scan_tagdir(platform::File& adi_f) {
    AdiIndex::Page pg2;
    auto got = adi_f.read_at(2 * ADI_PAGE_SIZE, pg2.data(), pg2.size());
    if (!got || got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "can't read ADI tag directory", ""};

    std::uint16_t count = u16_le(pg2.data() + 2);
    std::vector<TagEntry> tags;
    tags.reserve(count);

    for (std::uint16_t i = 0; i < count; ++i) {
        std::size_t off = ADI_TAGDIR_ENTRY_START + i * ADI_TAGDIR_ENTRY_SIZE;
        if (off + 1 >= ADI_PAGE_SIZE) break;
        std::uint8_t  xx      = pg2[off];
        std::uint32_t fmk_pg  = static_cast<std::uint32_t>(xx) + 1u;
        std::uint32_t root_pg = fmk_pg + 1u;

        auto fmk = read_one_page(adi_f, fmk_pg);
        if (!fmk) continue;
        auto fnums = parse_fmarker_all(fmk.value());
        if (fnums.empty()) continue;

        // Per-tag header is at page xx. Byte[14] bit 0 = unique flag.
        bool uniq = false;
        auto hdr_pg = read_one_page(adi_f, static_cast<std::uint32_t>(xx));
        if (hdr_pg) uniq = (hdr_pg.value()[14] & 0x01u) != 0;

        tags.push_back({std::move(fnums), root_pg, uniq});
    }
    return tags;
}

} // anonymous namespace

// ── AdiIndex::read_adi_page_ ─────────────────────────────────────────────────

util::Result<void> AdiIndex::read_adi_page_(std::uint32_t page_no,
                                            Page& buf) {
    auto got = adi_file_.read_at(
        static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
        buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "short ADI page read", ""};
    return {};
}

// ── AdiIndex::load_dense_leaf_ ───────────────────────────────────────────────

util::Result<void> AdiIndex::load_dense_leaf_(std::uint32_t page_no) {
    if (auto r = read_adi_page_(page_no, cur_page_); !r) return r;
    cur_pg_   = page_no;
    cur_cnt_  = page_count(cur_page_.data());
    cur_lsib_ = page_lsib(cur_page_.data());
    cur_rsib_ = page_rsib(cur_page_.data());
    cur_idx_  = -1;
    return {};
}

// ── AdiIndex::refresh_current_ ───────────────────────────────────────────────

util::Result<void> AdiIndex::refresh_current_() {
    if (cur_pg_ == ADI_INVALID_PAGE || cur_idx_ < 0 ||
        cur_idx_ >= static_cast<std::int32_t>(cur_cnt_)) {
        cur_recno_ = 0;
        current_key_.clear();
        return {};
    }
    cur_recno_ = dense_entry_recno(cur_page_.data(), cur_idx_, entry_size_);
    auto k = key_for_recno_(cur_recno_);
    if (!k) return k.error();
    current_key_ = std::move(k).value();
    return {};
}

// ── AdiIndex::branch_entry_page_ ────────────────────────────────────────────

std::uint32_t AdiIndex::branch_entry_page_(const std::uint8_t* pg,
                                           int idx) const noexcept {
    if (char_key_)
        return char_tree_entry_page(pg, idx, branch_entry_sz_,
                                    char_key_padded_len_);
    return tree_entry_page(pg, idx);
}

// ── AdiIndex::key_for_recno_ ─────────────────────────────────────────────────

util::Result<std::string> AdiIndex::key_for_recno_(std::uint32_t recno) {
    if (recno == 0 || adt_rec_len_ == 0)
        return std::string(key_total_len_, '\0');

    // ADT records are 1-based; offset past the 1-byte deleted flag is in fld_offset_
    std::uint64_t rec_off = static_cast<std::uint64_t>(adt_hdr_len_)
                          + static_cast<std::uint64_t>(recno - 1) * adt_rec_len_;

    // Build the full (possibly compound) key by concatenating all components.
    std::string result;
    result.reserve(key_total_len_);

    for (const auto& kf : key_fields_) {
        std::vector<std::uint8_t> buf(kf.length);
        auto got = adt_file_.read_at(rec_off + kf.offset, buf.data(), kf.length);
        if (!got || got.value() < kf.length) {
            // Unreadable field: pad with zeros / spaces
            bool is_c = (kf.type == ADT_TYPE_CICHAR || kf.type == ADT_TYPE_CHAR);
            result.append(is_c ? kf.length : 8u, is_c ? ' ' : '\0');
        } else {
            result += encode_adt_key(kf.type, buf.data(), kf.length);
        }
    }
    return result;
}

// ── AdiIndex::compare_keys_ ─────────────────────────────────────────────────

int AdiIndex::compare_keys_(const std::string& a,
                             const std::string& b) const noexcept {
    std::size_t len = std::min({a.size(), b.size(),
                                static_cast<std::size_t>(key_total_len_)});
    return std::memcmp(a.data(), b.data(), len);
}

// ── AdiIndex::make_positioned_ ──────────────────────────────────────────────

SeekOutcome AdiIndex::make_positioned_() const {
    SeekOutcome o;
    o.hit        = SeekHit::Exact;
    o.recno      = cur_recno_;
    o.positioned = true;
    return o;
}

// ── AdiIndex::navigate_leftmost_ ────────────────────────────────────────────

util::Result<SeekOutcome> AdiIndex::navigate_leftmost_() {
    Page pg{};
    std::uint32_t cur = root_page_;
    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv = page_level(pg.data());
        std::uint16_t ct = page_count(pg.data());
        if (ct == 0) {
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (is_dense_leaf(lv)) {
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = 0;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        // Branch or sparse leaf: follow the first entry's child page.
        cur = branch_entry_page_(pg.data(), 0);
    }
}

// ── AdiIndex::navigate_rightmost_ ──────────────────────────────────────────

util::Result<SeekOutcome> AdiIndex::navigate_rightmost_() {
    Page pg{};
    std::uint32_t cur = root_page_;
    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv = page_level(pg.data());
        std::uint16_t ct = page_count(pg.data());
        if (ct == 0) {
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (is_dense_leaf(lv)) {
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = static_cast<std::int32_t>(ct) - 1;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        cur = branch_entry_page_(pg.data(), static_cast<int>(ct) - 1);
    }
}

// ── IIndex public navigation ─────────────────────────────────────────────────

// ── AdiIndex::apply_tag_ ────────────────────────────────────────────────────

util::Result<void> AdiIndex::apply_tag_(
    const std::vector<std::uint8_t>& fnums,
    std::uint32_t                    root_pg,
    const std::vector<std::uint16_t>& fd_types,
    const std::vector<std::uint16_t>& fd_offsets,
    const std::vector<std::uint16_t>& fd_lengths,
    const std::vector<std::string>&   fd_names,
    std::uint32_t hlen, std::uint32_t rlen,
    bool unique)
{
    if (fnums.empty())
        return util::Error{5004, 0, "ADI tag has no field numbers", ""};

    key_fields_.clear();
    std::uint32_t total_key_len = 0;
    bool first = true;

    for (std::uint8_t fnum : fnums) {
        if (fnum == 0 || fnum > static_cast<unsigned>(fd_types.size()))
            return util::Error{5004, 0, "ADI field number out of range", ""};
        std::size_t fi = static_cast<std::size_t>(fnum) - 1u;  // 1-based → 0-based

        FieldComp kf;
        kf.type   = fd_types[fi];
        kf.offset = fd_offsets[fi];
        kf.length = fd_lengths[fi];
        key_fields_.push_back(kf);

        bool is_c = (kf.type == ADT_TYPE_CICHAR || kf.type == ADT_TYPE_CHAR);
        total_key_len += is_c ? static_cast<std::uint32_t>(kf.length) : 8u;

        if (first) {
            tag_name_   = fd_names[fi];
            root_page_  = root_pg;
            adt_type_   = kf.type;
            fld_offset_ = kf.offset;
            fld_length_ = kf.length;
            char_key_   = is_c;
            first = false;
        }
    }

    adt_hdr_len_   = hlen;
    adt_rec_len_   = rlen;
    key_total_len_ = total_key_len;
    entry_size_    = dense_entry_size(fld_length_);
    unique_        = unique;

    if (char_key_) {
        char_key_padded_len_ = (total_key_len + 3u) & ~3u;
        branch_entry_sz_     = char_key_padded_len_ + 5u;
    } else {
        char_key_padded_len_ = 0;
        branch_entry_sz_     = ADI_TREE_ENTRY_SIZE;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────

util::Result<void> AdiIndex::open(const std::string& path, IndexOpenMode mode) {
    mode_ = mode;
    auto fi = platform::File::open(path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = path;

    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();
    if (tags.value().empty())
        return util::Error{5004, 0, "ADI has no tags", path};

    std::string adt_p = adt_path_for(path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    const TagEntry& tag = tags.value()[0];
    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }
    return apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths, names,
                      hlen, rlen, tag.unique);
}

util::Result<void> AdiIndex::open_named(const std::string& adi_path,
                                        IndexOpenMode       mode,
                                        const std::string&  field_name) {
    mode_ = mode;
    auto fi = platform::File::open(adi_path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = adi_path;

    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();

    std::string adt_p = adt_path_for(adi_path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    auto name_eq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };

    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }

    for (const auto& tag : tags.value()) {
        if (tag.fnums.empty()) continue;
        std::uint8_t fnum = tag.fnums[0];
        if (fnum == 0 || fnum > static_cast<unsigned>(fields.value().size())) continue;
        const auto& fd = fields.value()[fnum - 1];
        if (!name_eq(fd.name, field_name)) continue;
        return apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths, names,
                          hlen, rlen, tag.unique);
    }
    return util::Error{5004, 0, "ADI tag not found: " + field_name, adi_path};
}

// static
util::Result<std::vector<std::string>>
AdiIndex::list_tags(const std::string& adi_path) {
    auto fi = platform::File::open(adi_path, platform::OpenMode::ReadOnly);
    if (!fi) return fi.error();
    platform::File adi_f = std::move(fi).value();

    auto tags = scan_tagdir(adi_f);
    if (!tags) return tags.error();

    std::string adt_p = adt_path_for(adi_path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    platform::File adt_f = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_f, hlen, rlen);
    if (!fields) return fields.error();
    (void)hlen; (void)rlen;

    std::vector<std::string> names;
    names.reserve(tags.value().size());
    for (const auto& tag : tags.value()) {
        if (tag.fnums.empty()) continue;
        std::uint8_t fnum = tag.fnums[0];
        if (fnum == 0 || fnum > static_cast<unsigned>(fields.value().size())) continue;
        names.push_back(fields.value()[fnum - 1].name);
    }
    return names;
}

util::Result<SeekOutcome> AdiIndex::seek_first() {
    return navigate_leftmost_();
}

util::Result<SeekOutcome> AdiIndex::seek_last() {
    return navigate_rightmost_();
}

util::Result<SeekOutcome> AdiIndex::next() {
    if (cur_pg_ == ADI_INVALID_PAGE) {
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    ++cur_idx_;
    if (cur_idx_ < static_cast<std::int32_t>(cur_cnt_)) {
        if (auto r = refresh_current_(); !r) return r.error();
        return make_positioned_();
    }
    // Move to right sibling dense leaf
    if (cur_rsib_ == ADI_INVALID_PAGE) {
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
    cur_idx_ = 0;
    if (cur_cnt_ == 0) {
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    if (auto r = refresh_current_(); !r) return r.error();
    return make_positioned_();
}

util::Result<SeekOutcome> AdiIndex::prev() {
    if (cur_pg_ == ADI_INVALID_PAGE) {
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    --cur_idx_;
    if (cur_idx_ >= 0) {
        if (auto r = refresh_current_(); !r) return r.error();
        return make_positioned_();
    }
    // Move to left sibling
    if (cur_lsib_ == ADI_INVALID_PAGE) {
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    if (auto r = load_dense_leaf_(cur_lsib_); !r) return r.error();
    cur_idx_ = static_cast<std::int32_t>(cur_cnt_) - 1;
    if (cur_idx_ < 0) {
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    if (auto r = refresh_current_(); !r) return r.error();
    return make_positioned_();
}

// ── AdiIndex::seek_key ───────────────────────────────────────────────────────
//
// Descends the B-tree from root to a dense leaf page, then linear-scans that
// leaf (and its right sibling if needed) by reading actual ADT field values.
// Handles both char-key (padded, memcmp) and numeric (8-byte sign-flipped BE).

util::Result<SeekOutcome> AdiIndex::seek_key(const std::string& key, bool soft) {
    Page pg{};
    std::uint32_t dense_pg = ADI_INVALID_PAGE;

    if (char_key_) {
        // Char-key ADI: branch (lv=1) → dense (lv=2); no sparse leaf level.
        // Branch entry key occupies char_key_padded_len_ bytes; incoming key
        // is key_total_len_ bytes (raw field length, ≤ padded length).
        std::uint32_t cur = root_page_;
        for (;;) {
            if (auto r = read_adi_page_(cur, pg); !r) return r.error();
            std::uint16_t lv  = page_level(pg.data());
            std::uint16_t cnt = page_count(pg.data());
            if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }
            if (is_dense_leaf(lv)) { dense_pg = cur; break; }
            // Branch: take first entry whose key >= seek key.
            int chosen = static_cast<int>(cnt) - 1;
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = pg.data() + ADI_TREE_ENTRY_START
                    + static_cast<std::uint32_t>(i) * branch_entry_sz_;
                if (std::memcmp(key.data(), ek, key_total_len_) <= 0) {
                    chosen = i; break;
                }
            }
            cur = branch_entry_page_(pg.data(), chosen);
        }
    } else {
        // Numeric-key ADI: descend until we hit a dense leaf.
        // Works for any depth (branch→dense, branch→sparse→dense, root=dense).
        if (key.size() != 8) return navigate_leftmost_();

        std::uint32_t cur = root_page_;
        for (;;) {
            if (auto r = read_adi_page_(cur, pg); !r) return r.error();
            std::uint16_t lv  = page_level(pg.data());
            std::uint16_t cnt = page_count(pg.data());
            if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }
            if (is_dense_leaf(lv)) { dense_pg = cur; break; }
            // Branch (lv=1) or sparse leaf (lv=0): pick child by key comparison.
            int chosen = static_cast<int>(cnt) - 1;
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = tree_entry_key(pg.data(), i);
                if (std::memcmp(key.data(), ek, 8) <= 0) { chosen = i; break; }
            }
            cur = tree_entry_page(pg.data(), chosen);
        }
    }

    // ── Dense leaf: linear scan via ADT field read ────────────────────────────
    if (auto r = load_dense_leaf_(dense_pg); !r) return r.error();

    for (int i = 0; i < static_cast<int>(cur_cnt_); ++i) {
        std::uint32_t rno = dense_entry_recno(cur_page_.data(), i, entry_size_);
        auto ck = key_for_recno_(rno);
        if (!ck) return ck.error();
        int cmp = compare_keys_(ck.value(), key);
        if (cmp > 0) {
            if (soft) {
                cur_idx_     = i;
                cur_recno_   = rno;
                current_key_ = std::move(ck).value();
                SeekOutcome o;
                o.hit        = SeekHit::AfterKey;
                o.recno      = cur_recno_;
                o.positioned = true;
                return o;
            }
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (cmp == 0) {
            cur_idx_     = i;
            cur_recno_   = rno;
            current_key_ = std::move(ck).value();
            return make_positioned_();
        }
    }
    // Key beyond this dense leaf: try right sibling.
    if (cur_rsib_ != ADI_INVALID_PAGE) {
        if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
        if (cur_cnt_ > 0) {
            cur_idx_ = 0;
            if (auto r = refresh_current_(); !r) return r.error();
            if (soft) {
                SeekOutcome o;
                o.hit        = SeekHit::AfterKey;
                o.recno      = cur_recno_;
                o.positioned = true;
                return o;
            }
        }
    }
    SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
}

// ── AdiIndex::write_adi_page_ ────────────────────────────────────────────────

util::Result<void> AdiIndex::write_adi_page_(std::uint32_t page_no,
                                             const Page& buf) {
    auto r = adi_file_.write_at(
        static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
        buf.data(), buf.size());
    if (!r) return r.error();
    if (r.value() != ADI_PAGE_SIZE)
        return util::Error{5000, 0, "short ADI page write", ""};
    return {};
}

// ── AdiIndex::alloc_page_ ───────────────────────────────────────────────────

util::Result<std::uint32_t> AdiIndex::alloc_page_() {
    auto sz = adi_file_.size();
    if (!sz) return sz.error();
    return static_cast<std::uint32_t>(sz.value() / ADI_PAGE_SIZE);
}

// ── AdiIndex::build_dense_entry_ ────────────────────────────────────────────

void AdiIndex::build_dense_entry_(std::uint8_t* dst, std::uint32_t recno,
                                  const std::string& ikey) const noexcept {
    if (entry_size_ >= 3) {
        dst[0] = static_cast<std::uint8_t>(recno);
        dst[1] = static_cast<std::uint8_t>(recno >> 8);
        dst[2] = 0x00;  // type/flags byte — unknown; 0 works for new inserts
    } else {
        // 2-byte entry: recno(1) + key_flags(1).  For LOGICAL fields the
        // key_flags byte encodes the boolean value (0x00=false, 0x40=true).
        dst[0] = static_cast<std::uint8_t>(recno);
        dst[1] = ikey.empty() ? std::uint8_t{0}
                              : static_cast<std::uint8_t>(ikey[0]);
    }
}

// ── AdiIndex::branch_key_at_ ────────────────────────────────────────────────

std::string AdiIndex::branch_key_at_(const std::uint8_t* pg, int idx) const noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * branch_entry_sz_;
    if (char_key_)
        return std::string(reinterpret_cast<const char*>(e), key_total_len_);
    return std::string(reinterpret_cast<const char*>(e), 8);
}

// ── AdiIndex::promote_split_ ─────────────────────────────────────────────────
//
// Push a split result up the path stack.  Called after a leaf split with:
//   left_pg  – page that ALREADY holds the left half (unchanged or rewritten)
//   left_max – max key of left_pg's subtree
//   right_pg – newly allocated page holding the right half
//   right_max – max key of right_pg's subtree
//
// If path is empty the root page is rewritten as a 2-entry branch.
// Otherwise pops the top frame, inserts right_pg into the parent branch,
// and may trigger a branch split (recursive call).

util::Result<void> AdiIndex::promote_split_(
    std::vector<PathFrame>& path,
    std::uint32_t left_pg,  const std::string& left_max,
    std::uint32_t right_pg, const std::string& right_max)
{
    const std::uint32_t max_branch = (ADI_PAGE_SIZE - ADI_TREE_ENTRY_START)
                                     / branch_entry_sz_;

    auto write_branch_entry = [&](std::uint8_t* dst, const std::string& key,
                                  std::uint32_t page_no) {
        if (char_key_) {
            // padded_key[char_key_padded_len_] + cum[4 LE]=0 + page[1]
            std::memset(dst, 0, branch_entry_sz_);
            std::size_t klen = std::min((std::size_t)key_total_len_, key.size());
            std::memcpy(dst, key.data(), klen);
            dst[char_key_padded_len_ + 4] = static_cast<std::uint8_t>(page_no & 0xFFu);
        } else {
            // key[8 BE] + cum[4 BE]=0 + page[4 BE]
            std::memset(dst, 0, 16);
            std::size_t klen = std::min<std::size_t>(8, key.size());
            std::memcpy(dst, key.data(), klen);
            set_u32_be(dst + 12, page_no);
        }
    };

    if (path.empty()) {
        // Root was the leaf (or we've bubbled all the way up).
        // Rewrite root_page_ as a 2-entry branch.
        Page root{};
        set_u16_le(root.data(), ADI_LVL_BRANCH);
        set_u16_le(root.data() + 2, 2);
        set_u32_le(root.data() + 4, ADI_INVALID_PAGE);
        set_u32_le(root.data() + 8, ADI_INVALID_PAGE);
        write_branch_entry(root.data() + ADI_TREE_ENTRY_START,
                           left_max,  left_pg);
        write_branch_entry(root.data() + ADI_TREE_ENTRY_START + branch_entry_sz_,
                           right_max, right_pg);
        return write_adi_page_(root_page_, root);
    }

    // Pop parent frame.
    PathFrame frame = path.back();
    path.pop_back();

    // Read the parent branch page.
    Page parent{};
    if (auto r = read_adi_page_(frame.page_no, parent); !r) return r;

    std::uint16_t par_cnt = page_count(parent.data());

    // Build a combined branch-entry buffer: all existing entries plus the new
    // right child entry.  We also update the existing entry[frame.entry_idx]
    // key to reflect the new left_max.
    std::uint32_t total = par_cnt + 1;
    std::vector<std::uint8_t> combo(total * branch_entry_sz_);

    std::uint8_t* src = parent.data() + ADI_TREE_ENTRY_START;
    // Copy entries [0..entry_idx], updating the chosen entry's key.
    for (int i = 0; i <= frame.entry_idx; ++i) {
        std::uint8_t* dst = combo.data() + i * branch_entry_sz_;
        std::memcpy(dst, src + i * branch_entry_sz_, branch_entry_sz_);
        if (i == frame.entry_idx) {
            // Update this entry's key to left_max; page pointer stays.
            std::size_t klen = std::min((std::size_t)(char_key_ ? key_total_len_ : 8u),
                                        left_max.size());
            std::memcpy(dst, left_max.data(), klen);
            if (char_key_ && key_total_len_ < char_key_padded_len_)
                std::memset(dst + key_total_len_, 0,
                            char_key_padded_len_ - key_total_len_);
        }
    }
    // New entry for right child at entry_idx+1
    write_branch_entry(combo.data() + (frame.entry_idx + 1) * branch_entry_sz_,
                       right_max, right_pg);
    // Copy remaining entries [entry_idx+1..par_cnt-1] shifted right by one.
    for (std::uint32_t i = frame.entry_idx + 1; i < par_cnt; ++i) {
        std::memcpy(combo.data() + (i + 1) * branch_entry_sz_,
                    src + i * branch_entry_sz_, branch_entry_sz_);
    }

    if (total <= max_branch) {
        // Fits: write updated parent.
        set_u16_le(parent.data() + 2, static_cast<std::uint16_t>(total));
        std::memcpy(src, combo.data(), total * branch_entry_sz_);
        return write_adi_page_(frame.page_no, parent);
    }

    // Branch is full: split it.
    std::uint32_t left_cnt  = total / 2;
    std::uint32_t right_cnt = total - left_cnt;

    // Extract max key from a combo-buffer branch entry (no ADT read needed).
    auto combo_key = [&](std::uint32_t idx) -> std::string {
        const std::uint8_t* e = combo.data() + idx * branch_entry_sz_;
        std::size_t klen = char_key_ ? key_total_len_ : 8u;
        return std::string(reinterpret_cast<const char*>(e), klen);
    };
    std::string new_left_max  = combo_key(left_cnt - 1);
    std::string new_right_max = combo_key(total - 1);

    // Allocate right branch page.
    auto rp_r = alloc_page_();
    if (!rp_r) return rp_r.error();
    std::uint32_t right_branch_pg = rp_r.value();

    // Rewrite parent (left half).
    set_u16_le(parent.data() + 2, static_cast<std::uint16_t>(left_cnt));
    std::memcpy(src, combo.data(), left_cnt * branch_entry_sz_);
    if (auto r = write_adi_page_(frame.page_no, parent); !r) return r;

    // Write right branch page.
    Page right_branch{};
    set_u16_le(right_branch.data(), ADI_LVL_BRANCH);
    set_u16_le(right_branch.data() + 2, static_cast<std::uint16_t>(right_cnt));
    set_u32_le(right_branch.data() + 4, ADI_INVALID_PAGE);
    set_u32_le(right_branch.data() + 8, ADI_INVALID_PAGE);
    std::memcpy(right_branch.data() + ADI_TREE_ENTRY_START,
                combo.data() + left_cnt * branch_entry_sz_,
                right_cnt * branch_entry_sz_);
    if (auto r = write_adi_page_(right_branch_pg, right_branch); !r) return r;

    // Recurse: promote branch split.
    return promote_split_(path,
                          frame.page_no, new_left_max,
                          right_branch_pg, new_right_max);
}

// ── AdiIndex::insert ─────────────────────────────────────────────────────────

util::Result<void> AdiIndex::insert(std::uint32_t recno,
                                    const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};

    // Normalise key to key_total_len_ bytes.
    std::string ikey = key;
    if (char_key_) {
        if (ikey.size() < key_total_len_)
            ikey.append(key_total_len_ - ikey.size(), ' ');
        else
            ikey.resize(key_total_len_);
    } else {
        ikey.resize(8, '\0');
    }

    // ── Descend from root, building the path stack ───────────────────────────
    std::vector<PathFrame> path;
    Page pg{};
    std::uint32_t cur = root_page_;

    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv  = page_level(pg.data());
        std::uint16_t cnt = page_count(pg.data());
        if (is_dense_leaf(lv)) break;

        int chosen = cnt ? static_cast<int>(cnt) - 1 : 0;
        if (char_key_) {
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = pg.data() + ADI_TREE_ENTRY_START
                    + static_cast<std::uint32_t>(i) * branch_entry_sz_;
                if (std::memcmp(ikey.data(), ek, key_total_len_) <= 0) {
                    chosen = i; break;
                }
            }
        } else {
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                if (std::memcmp(ikey.data(), tree_entry_key(pg.data(), i), 8) <= 0) {
                    chosen = i; break;
                }
            }
        }
        path.push_back({cur, cnt, chosen});
        cur = branch_entry_page_(pg.data(), chosen);
    }

    std::uint16_t leaf_lv  = page_level(pg.data());
    std::uint16_t leaf_cnt = page_count(pg.data());
    const std::uint32_t max_ents =
        (ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START) / entry_size_;

    // ── Binary-search for the insertion position ─────────────────────────────
    int pos;
    {
        int lo = 0, hi = static_cast<int>(leaf_cnt);
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            std::uint32_t mrec = dense_entry_recno(pg.data(), mid, entry_size_);
            auto mk = key_for_recno_(mrec);
            if (!mk) return mk.error();
            int cmp = compare_keys_(mk.value(), ikey);
            if (cmp < 0 || (cmp == 0 && mrec < recno)) lo = mid + 1;
            else hi = mid;
        }
        pos = lo;
    }

    // ── Simple insert (leaf not full) ────────────────────────────────────────
    if (leaf_cnt < max_ents) {
        std::uint8_t* base = pg.data() + ADI_DENSE_ENTRY_START;
        std::uint32_t move_n = leaf_cnt - static_cast<std::uint32_t>(pos);
        if (move_n > 0)
            std::memmove(base + (pos + 1) * entry_size_,
                         base + pos         * entry_size_,
                         move_n * entry_size_);
        build_dense_entry_(base + pos * entry_size_, recno, ikey);
        set_u16_le(pg.data() + 2, leaf_cnt + 1);

        // Keep cursor consistent.
        if (cur_pg_ == cur) {
            cur_page_ = pg;
            cur_cnt_  = leaf_cnt + 1;
            if (cur_idx_ >= pos) ++cur_idx_;
        }
        return write_adi_page_(cur, pg);
    }

    // ── Leaf is full: build combined buffer and split ─────────────────────────
    std::vector<std::uint8_t> combo((max_ents + 1) * entry_size_);
    std::uint8_t* base = pg.data() + ADI_DENSE_ENTRY_START;
    std::memcpy(combo.data(),
                base,
                static_cast<std::uint32_t>(pos) * entry_size_);
    build_dense_entry_(combo.data() + pos * entry_size_, recno, ikey);
    std::memcpy(combo.data() + (pos + 1) * entry_size_,
                base + pos * entry_size_,
                (max_ents - static_cast<std::uint32_t>(pos)) * entry_size_);

    const std::uint32_t total   = max_ents + 1;
    const std::uint32_t lft_cnt = total / 2;
    const std::uint32_t rgt_cnt = total - lft_cnt;

    std::uint32_t orig_lsib = page_lsib(pg.data());
    std::uint32_t orig_rsib = page_rsib(pg.data());

    if (path.empty()) {
        // Root is the dense leaf.  Allocate TWO new pages; root becomes branch.
        // Must allocate and WRITE left page before allocating right (file size grows).
        auto lp_r = alloc_page_();
        if (!lp_r) return lp_r.error();
        std::uint32_t left_pg = lp_r.value();

        // We don't know right_pg yet, so write left with a placeholder rsib,
        // then patch it after allocating right.
        Page left_page = pg;  // copy header (lv, sub-header, etc.)
        set_u16_le(left_page.data() + 2, static_cast<std::uint16_t>(lft_cnt));
        set_u32_le(left_page.data() + 4, orig_lsib);
        set_u32_le(left_page.data() + 8, ADI_INVALID_PAGE);  // filled in below
        std::memcpy(left_page.data() + ADI_DENSE_ENTRY_START,
                    combo.data(), lft_cnt * entry_size_);
        if (auto r = write_adi_page_(left_pg, left_page); !r) return r;
        // File has grown; now allocate right page.
        auto rp_r = alloc_page_();
        if (!rp_r) return rp_r.error();
        std::uint32_t right_pg = rp_r.value();
        // Patch left_page.rsib.
        set_u32_le(left_page.data() + 8, right_pg);
        if (auto r = write_adi_page_(left_pg, left_page); !r) return r;

        // Build right leaf.
        Page right_page{};
        set_u16_le(right_page.data(), leaf_lv);
        set_u16_le(right_page.data() + 2, static_cast<std::uint16_t>(rgt_cnt));
        set_u32_le(right_page.data() + 4, left_pg);
        set_u32_le(right_page.data() + 8, orig_rsib);
        std::memcpy(right_page.data() + 12, pg.data() + 12, 12);  // sub-header
        std::memcpy(right_page.data() + ADI_DENSE_ENTRY_START,
                    combo.data() + lft_cnt * entry_size_,
                    rgt_cnt * entry_size_);
        if (auto r = write_adi_page_(right_pg, right_page); !r) return r;

        // Update old right sibling's lsib pointer.
        if (orig_rsib != ADI_INVALID_PAGE) {
            Page rsib_pg{};
            if (auto r = read_adi_page_(orig_rsib, rsib_pg); !r) return r;
            set_u32_le(rsib_pg.data() + 4, right_pg);
            if (auto r = write_adi_page_(orig_rsib, rsib_pg); !r) return r;
        }

        // Invalidate cursor (root content changed completely).
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1; cur_cnt_ = 0;

        // Get max keys then rewrite root as branch.
        auto left_max = key_for_recno_(dense_recno_from_buf(
            combo.data(), lft_cnt - 1, entry_size_));
        if (!left_max) return left_max.error();
        auto right_max = key_for_recno_(dense_recno_from_buf(
            combo.data(), total - 1, entry_size_));
        if (!right_max) return right_max.error();

        return promote_split_(path, left_pg,  left_max.value(),
                                    right_pg, right_max.value());
    }

    // Non-root split: left half stays in cur, right half goes to new page.
    auto rp_r = alloc_page_();
    if (!rp_r) return rp_r.error();
    std::uint32_t right_pg = rp_r.value();

    // Rewrite cur (left leaf) with left half.
    set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(lft_cnt));
    set_u32_le(pg.data() + 8, right_pg);
    std::memcpy(pg.data() + ADI_DENSE_ENTRY_START,
                combo.data(), lft_cnt * entry_size_);
    if (auto r = write_adi_page_(cur, pg); !r) return r;

    // Build and write right leaf.
    Page right_page{};
    set_u16_le(right_page.data(), leaf_lv);
    set_u16_le(right_page.data() + 2, static_cast<std::uint16_t>(rgt_cnt));
    set_u32_le(right_page.data() + 4, cur);
    set_u32_le(right_page.data() + 8, orig_rsib);
    std::memcpy(right_page.data() + 12, pg.data() + 12, 12);  // sub-header
    std::memcpy(right_page.data() + ADI_DENSE_ENTRY_START,
                combo.data() + lft_cnt * entry_size_,
                rgt_cnt * entry_size_);
    if (auto r = write_adi_page_(right_pg, right_page); !r) return r;

    // Update orig_rsib.lsib.
    if (orig_rsib != ADI_INVALID_PAGE) {
        Page rsib_pg{};
        if (auto r = read_adi_page_(orig_rsib, rsib_pg); !r) return r;
        set_u32_le(rsib_pg.data() + 4, right_pg);
        if (auto r = write_adi_page_(orig_rsib, rsib_pg); !r) return r;
    }

    // Cursor may be in either half; invalidate to be safe.
    if (cur_pg_ == cur) {
        cur_page_ = pg;
        cur_cnt_  = static_cast<std::uint16_t>(lft_cnt);
        cur_rsib_ = right_pg;
        if (cur_idx_ >= static_cast<std::int32_t>(lft_cnt)) {
            cur_pg_  = ADI_INVALID_PAGE;
            cur_idx_ = -1;
        }
    }

    auto left_max = key_for_recno_(dense_recno_from_buf(
        combo.data(), lft_cnt - 1, entry_size_));
    if (!left_max) return left_max.error();
    auto right_max = key_for_recno_(dense_recno_from_buf(
        combo.data(), total - 1, entry_size_));
    if (!right_max) return right_max.error();

    return promote_split_(path, cur,      left_max.value(),
                                right_pg, right_max.value());
}

// ── AdiIndex::erase ──────────────────────────────────────────────────────────

util::Result<void> AdiIndex::erase(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};

    // Normalise key.
    std::string ikey = key;
    if (char_key_) {
        if (ikey.size() < key_total_len_)
            ikey.append(key_total_len_ - ikey.size(), ' ');
        else
            ikey.resize(key_total_len_);
    } else {
        ikey.resize(8, '\0');
    }

    // Seek to the correct dense leaf (soft=true: positions at or after key).
    auto sk = seek_key(ikey, /*soft=*/true);
    if (!sk) return sk.error();
    if (sk.value().hit == SeekHit::AfterEnd || !sk.value().positioned)
        return util::Error{5044, 0, "ADI: key not found for erase", ""};

    // Scan forward from the seek position to find the exact (key, recno) entry.
    for (;;) {
        if (cur_pg_ == ADI_INVALID_PAGE) break;
        for (int i = cur_idx_; i < static_cast<int>(cur_cnt_); ++i) {
            std::uint32_t erec = dense_entry_recno(cur_page_.data(), i, entry_size_);
            auto ek = key_for_recno_(erec);
            if (!ek) return ek.error();
            int cmp = compare_keys_(ek.value(), ikey);
            if (cmp > 0)
                return util::Error{5044, 0, "ADI: key not found for erase", ""};
            if (cmp == 0 && erec == recno) {
                // Found — remove entry i.
                std::uint8_t* base = cur_page_.data() + ADI_DENSE_ENTRY_START;
                std::uint32_t move_n = static_cast<std::uint32_t>(cur_cnt_) - 1
                                       - static_cast<std::uint32_t>(i);
                if (move_n > 0)
                    std::memmove(base + i * entry_size_,
                                 base + (i + 1) * entry_size_,
                                 move_n * entry_size_);
                --cur_cnt_;
                set_u16_le(cur_page_.data() + 2, cur_cnt_);

                // Adjust cursor index.
                if (cur_idx_ >= static_cast<int>(cur_cnt_)) {
                    cur_idx_ = static_cast<int>(cur_cnt_) - 1;
                }

                // Remember the page number before we potentially clear cur_pg_.
                std::uint32_t write_pg = cur_pg_;

                if (cur_cnt_ == 0) {
                    // Page is now empty: bypass it in sibling links.
                    std::uint32_t lsib = page_lsib(cur_page_.data());
                    std::uint32_t rsib = page_rsib(cur_page_.data());
                    if (lsib != ADI_INVALID_PAGE) {
                        Page lp{};
                        if (auto r = read_adi_page_(lsib, lp); !r) return r;
                        set_u32_le(lp.data() + 8, rsib);
                        if (auto r = write_adi_page_(lsib, lp); !r) return r;
                    }
                    if (rsib != ADI_INVALID_PAGE) {
                        Page rp{};
                        if (auto r = read_adi_page_(rsib, rp); !r) return r;
                        set_u32_le(rp.data() + 4, lsib);
                        if (auto r = write_adi_page_(rsib, rp); !r) return r;
                    }
                    cur_pg_  = ADI_INVALID_PAGE;
                    cur_idx_ = -1;
                }

                return write_adi_page_(write_pg, cur_page_);
            }
        }
        // Not on this leaf: advance to right sibling.
        if (cur_rsib_ == ADI_INVALID_PAGE) break;
        if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
        cur_idx_ = 0;
    }
    return util::Error{5044, 0, "ADI: key not found for erase", ""};
}

// ── AdiIndex::flush ──────────────────────────────────────────────────────────

util::Result<void> AdiIndex::flush() {
    return adi_file_.sync();
}

} // namespace openads::drivers::adi
