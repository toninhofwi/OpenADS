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
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
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

platform::OpenMode map_open_mode(IndexOpenMode m) noexcept {
    return (m == IndexOpenMode::ReadOnly)
               ? platform::OpenMode::ReadOnly
               : platform::OpenMode::OpenExisting;
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

// ── Branch/sparse entry (level 0 and 1): starts at offset 12 ─────────────────
// Format: key[8](BE sign-flipped float64) + cum[4](BE uint32) + page_no[4](BE uint32)

std::uint32_t tree_entry_page(const std::uint8_t* pg, int idx) noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START + idx * ADI_TREE_ENTRY_SIZE;
    return u32_be(e + 12);
}

const std::uint8_t* tree_entry_key(const std::uint8_t* pg, int idx) noexcept {
    return pg + ADI_TREE_ENTRY_START + idx * ADI_TREE_ENTRY_SIZE;
}

// ── Dense-leaf entry: starts at offset 24 ────────────────────────────────────
// Format: recno[1](byte) + key_or_dup_trail[1 or 2]
// entry_sz = dense_entry_size(fld_length): 2 for 1-byte fields, 3 for wider

std::uint32_t dense_entry_recno(const std::uint8_t* pg, int idx,
                                std::uint32_t entry_sz) noexcept {
    const std::uint8_t* e = pg + ADI_DENSE_ENTRY_START + idx * entry_sz;
    return e[0];  // 1-byte record number
}

// ── Key encoding ─────────────────────────────────────────────────────────────

// Pack a double into an 8-byte sign-flipped big-endian ADI key.
// The resulting 8 bytes sort correctly via memcmp for ascending doubles.
std::string pack_double_key(double v) {
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);               // raw = IEEE754 LE on x86
    // Convert LE → BE
    std::reverse(raw, raw + 8);
    // Flip sign bit so negative values sort before positive
    raw[0] ^= 0x80u;
    return std::string(reinterpret_cast<char*>(raw), 8);
}

// Unpack an ADI 8-byte key back to double (for debug / seek conversion)
double unpack_double_key(const std::string& key) {
    std::uint8_t raw[8];
    std::memcpy(raw, key.data(), 8);
    raw[0] ^= 0x80u;                       // un-flip sign
    std::reverse(raw, raw + 8);            // BE → LE
    double v;
    std::memcpy(&v, raw, 8);
    return v;
}

// Encode an ADT field value to ADI key bytes, given ADT type and field data
std::string encode_adt_key(std::uint16_t adt_type, const std::uint8_t* data,
                           std::uint16_t length) {
    double val = 0.0;
    switch (adt_type) {
        case ADT_TYPE_DATE: {
            // 4-byte LE uint32 JDN
            std::uint32_t jdn = u32_le(data);
            val = static_cast<double>(jdn);
            break;
        }
        case ADT_TYPE_AUTOINC:
        case ADT_TYPE_TIME: {
            // 4-byte LE uint32
            std::uint32_t v = u32_le(data);
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_INTEGER: {
            // 4-byte LE int32
            std::int32_t v = static_cast<std::int32_t>(u32_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_SHORTINT: {
            // 2-byte LE int16
            std::int16_t v = static_cast<std::int16_t>(u16_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_DOUBLE:
        case ADT_TYPE_MONEY:
        case ADT_TYPE_MODTIME: {
            // 8-byte IEEE754 LE (same bit pattern, just flip+reverse)
            std::uint8_t raw[8];
            std::memcpy(raw, data, 8);
            std::reverse(raw, raw + 8);
            raw[0] ^= 0x80u;
            return std::string(reinterpret_cast<char*>(raw), 8);
        }
        case ADT_TYPE_TIMESTAMP: {
            // 8-byte: 4-byte JDN LE + 4-byte ms LE
            // Key = double(JDN) * 86400000 + ms, treat as int64?
            // Simplify: treat full 8 bytes as uint64 LE
            std::uint64_t v = static_cast<std::uint64_t>(u32_le(data)) |
                              (static_cast<std::uint64_t>(u32_le(data+4)) << 32);
            val = static_cast<double>(v);
            break;
        }
        default:
            // Unsupported type: return zero key
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

// Parse "F<digits>" F-marker, return field number (1-based) or 0 on failure.
std::uint8_t parse_fmarker(const AdiIndex::Page& pg) noexcept {
    if (pg[0] != 'F') return 0;
    if (pg[1] < '1' || pg[1] > '9') return 0;
    std::uint32_t n = pg[1] - '0';
    for (std::size_t i = 2; i < 8 && pg[i] >= '0' && pg[i] <= '9'; ++i)
        n = n * 10 + (pg[i] - '0');
    // Must be followed by NUL
    return (n > 0 && n <= 255) ? static_cast<std::uint8_t>(n) : 0;
}

// Scan tag directory (page 2) and return (field_num, root_page) pairs in order.
util::Result<std::vector<std::pair<std::uint8_t,std::uint32_t>>>
scan_tagdir(platform::File& adi_f) {
    AdiIndex::Page pg2;
    auto got = adi_f.read_at(2 * ADI_PAGE_SIZE, pg2.data(), pg2.size());
    if (!got || got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "can't read ADI tag directory", ""};

    std::uint16_t count = u16_le(pg2.data() + 2);
    std::vector<std::pair<std::uint8_t,std::uint32_t>> tags;
    tags.reserve(count);

    for (std::uint16_t i = 0; i < count; ++i) {
        std::size_t off = ADI_TAGDIR_ENTRY_START + i * ADI_TAGDIR_ENTRY_SIZE;
        if (off + 1 >= ADI_PAGE_SIZE) break;
        std::uint8_t  xx       = pg2[off];         // F-marker page = xx + 1
        std::uint32_t fmk_pg   = xx + 1u;
        std::uint32_t root_pg  = fmk_pg + 1u;

        // Verify F-marker
        auto fmk = read_one_page(adi_f, fmk_pg);
        if (!fmk) continue;
        std::uint8_t fnum = parse_fmarker(fmk.value());
        if (fnum == 0) continue;

        tags.push_back({fnum, root_pg});
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

// ── AdiIndex::key_for_recno_ ─────────────────────────────────────────────────

util::Result<std::string> AdiIndex::key_for_recno_(std::uint32_t recno) {
    if (recno == 0 || adt_rec_len_ == 0)
        return std::string(8, '\0');

    // ADT records are 1-based
    std::uint64_t offset = static_cast<std::uint64_t>(adt_hdr_len_)
                         + static_cast<std::uint64_t>(recno - 1) * adt_rec_len_;

    // Read just the field bytes (field is within the record at fld_offset_)
    std::vector<std::uint8_t> buf(fld_length_);
    auto got = adt_file_.read_at(offset + fld_offset_, buf.data(), fld_length_);
    if (!got) return got.error();
    if (got.value() < fld_length_)
        return std::string(8, '\0');

    return encode_adt_key(adt_type_, buf.data(), fld_length_);
}

// ── AdiIndex::compare_keys_ ─────────────────────────────────────────────────

int AdiIndex::compare_keys_(const std::string& a, const std::string& b) noexcept {
    return std::memcmp(a.data(), b.data(), 8);
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
    // Descend branch → sparse leaf → dense leaf
    std::uint32_t cur = root_page_;
    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv = page_level(pg.data());
        std::uint16_t ct = page_count(pg.data());
        if (ct == 0) {
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (lv == ADI_LVL_DENSE) {
            // arrived at a dense leaf
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = 0;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        // branch or sparse leaf: follow first entry's child page
        cur = tree_entry_page(pg.data(), 0);
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
        if (lv == ADI_LVL_DENSE) {
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = static_cast<std::int32_t>(ct) - 1;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        cur = tree_entry_page(pg.data(), static_cast<int>(ct) - 1);
    }
}

// ── IIndex public navigation ─────────────────────────────────────────────────

util::Result<void> AdiIndex::open(const std::string& path, IndexOpenMode mode) {
    // open() is a thin wrapper: opens the first tag found
    auto fi = platform::File::open(path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = path;

    // Find first tag in directory
    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();
    if (tags.value().empty())
        return util::Error{5004, 0, "ADI has no tags", path};

    auto [fnum, root_pg] = tags.value()[0];

    // Open ADT companion
    std::string adt_p = adt_path_for(path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();
    if (fnum == 0 || fnum > static_cast<int>(fields.value().size()))
        return util::Error{5004, 0, "ADI field number out of range", ""};

    const auto& fd = fields.value()[fnum - 1]; // 1-based → 0-indexed
    tag_name_    = fd.name;
    root_page_   = root_pg;
    adt_type_    = fd.type;
    fld_offset_  = fd.offset;
    fld_length_  = fd.length;
    adt_hdr_len_ = hlen;
    adt_rec_len_ = rlen;
    entry_size_  = dense_entry_size(fld_length_);
    return {};
}

util::Result<void> AdiIndex::open_named(const std::string& adi_path,
                                        IndexOpenMode       mode,
                                        const std::string&  field_name) {
    auto fi = platform::File::open(adi_path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = adi_path;

    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();

    // Open ADT to get field names
    std::string adt_p = adt_path_for(adi_path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();
    adt_hdr_len_ = hlen;
    adt_rec_len_ = rlen;

    // Find the tag whose field name matches (case-insensitive)
    auto name_eq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };

    for (auto [fnum, root_pg] : tags.value()) {
        if (fnum == 0 || fnum > static_cast<int>(fields.value().size())) continue;
        const auto& fd = fields.value()[fnum - 1];
        if (!name_eq(fd.name, field_name)) continue;
        tag_name_   = fd.name;
        root_page_  = root_pg;
        adt_type_   = fd.type;
        fld_offset_ = fd.offset;
        fld_length_ = fd.length;
        entry_size_ = dense_entry_size(fld_length_);
        return {};
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
    for (auto [fnum, root_pg] : tags.value()) {
        (void)root_pg;
        if (fnum == 0 || fnum > static_cast<int>(fields.value().size())) continue;
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
// Navigates the 3-level B-tree (root → sparse → dense) using the 8-byte
// sign-flipped BE float64 key.  Within the dense leaf the position is found
// by comparing actual ADT field values (since dense leaves don't store keys).

util::Result<SeekOutcome> AdiIndex::seek_key(const std::string& key, bool soft) {
    if (key.size() != 8) {
        // Fall back to first for unsupported key sizes
        return navigate_leftmost_();
    }

    Page pg{};

    // ── Level 1 (branch/root) → find the sparse-leaf child ────────────────
    if (auto r = read_adi_page_(root_page_, pg); !r) return r.error();
    std::uint16_t lv  = page_level(pg.data());
    std::uint16_t cnt = page_count(pg.data());
    if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }

    // Walk branches until we reach a sparse leaf (level 0)
    while (lv != ADI_LVL_SPARSE && lv != ADI_LVL_DENSE) {
        // Find first entry whose key >= seek key
        int chosen = cnt - 1;
        for (int i = 0; i < cnt; ++i) {
            const std::uint8_t* ek = tree_entry_key(pg.data(), i);
            if (std::memcmp(key.data(), ek, 8) <= 0) { chosen = i; break; }
        }
        std::uint32_t child = tree_entry_page(pg.data(), chosen);
        if (auto r = read_adi_page_(child, pg); !r) return r.error();
        lv  = page_level(pg.data());
        cnt = page_count(pg.data());
        if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }
    }

    std::uint32_t dense_pg = ADI_INVALID_PAGE;

    if (lv == ADI_LVL_SPARSE) {
        // ── Level 0 (sparse leaf) → find the dense-leaf page ──────────────
        // Each sparse entry's key is the MAX key in the covered dense leaf.
        // Find first entry whose key >= seek key.
        int chosen = cnt - 1;
        for (int i = 0; i < cnt; ++i) {
            const std::uint8_t* ek = tree_entry_key(pg.data(), i);
            if (std::memcmp(key.data(), ek, 8) <= 0) { chosen = i; break; }
        }
        dense_pg = tree_entry_page(pg.data(), chosen);
    } else {
        // Arrived at dense leaf directly (tiny index with root=dense)
        dense_pg = root_page_;
        // reload
        if (auto r = read_adi_page_(dense_pg, pg); !r) return r.error();
        cnt = page_count(pg.data());
    }

    // ── Level 2 (dense leaf) → linear scan by comparing ADT field values ──
    if (auto r = load_dense_leaf_(dense_pg); !r) return r.error();

    // Scan entries: the dense leaf is sorted by key, find first entry >= key
    for (int i = 0; i < static_cast<int>(cur_cnt_); ++i) {
        std::uint32_t rno = dense_entry_recno(cur_page_.data(), i, entry_size_);
        auto ck = key_for_recno_(rno);
        if (!ck) return ck.error();
        int cmp = compare_keys_(ck.value(), key);
        if (cmp > 0) {
            // Overshot: key not found
            if (soft) {
                // Position before this entry
                cur_idx_ = i;
                cur_recno_ = rno;
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
            cur_idx_ = i;
            cur_recno_ = rno;
            current_key_ = std::move(ck).value();
            return make_positioned_();
        }
    }
    // Key is beyond everything in this dense leaf; try rsib
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

} // namespace openads::drivers::adi
