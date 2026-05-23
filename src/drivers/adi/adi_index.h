#pragma once

#include "drivers/index_trait.h"
#include "platform/file.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers::adi {

constexpr std::uint32_t ADI_PAGE_SIZE    = 512;
constexpr std::uint32_t ADI_INVALID_PAGE = 0xFFFFFFFFu;

// ADI B-tree level constants (observed from real ADI files)
// Levels 0-2 are unconfirmed (only seen in tiny tables with no branch pages).
// Level 3 is confirmed: both dense-leaf pages and the tag-directory page carry
// level=3 in their header, but the tag directory is always accessed directly by
// page number so the collision is harmless.
constexpr std::uint16_t ADI_LVL_SPARSE = 0;  // sparse leaf: ref → dense leaf
constexpr std::uint16_t ADI_LVL_BRANCH = 1;  // root/branch: ref → sparse leaf
constexpr std::uint16_t ADI_LVL_DENSE  = 3;  // dense leaf: individual record entries
constexpr std::uint16_t ADI_LVL_TAGDIR = 3;  // tag directory (page 2 only)

// Entry sizes
constexpr std::uint32_t ADI_TREE_ENTRY_SIZE   = 16; // key(8) + cum(4) + page(4)
// Dense-leaf entry size depends on field storage width:
//   1-byte fields (LOGICAL): recno(1) + key_value(1)     = 2 bytes
//   wider fields:            recno(1) + dup(1) + trail(1) = 3 bytes
// Use dense_entry_size(fld_length) to get the right value at runtime.
constexpr std::uint32_t ADI_TREE_ENTRY_START  = 12; // right after 12-byte page header
constexpr std::uint32_t ADI_DENSE_ENTRY_START = 24; // after header(12) + sub-header(12)
constexpr std::uint32_t ADI_TAGDIR_ENTRY_SIZE =  6; // XX(1) + zeros(4) + YY(1)
constexpr std::uint32_t ADI_TAGDIR_ENTRY_START= 24; // same as dense leaf

inline constexpr std::uint32_t dense_entry_size(std::uint16_t fld_length) noexcept {
    return (fld_length == 1u) ? 2u : 3u;
}

// ADT type codes that appear as ADI index keys (numeric → sign-flipped float64)
constexpr std::uint16_t ADT_TYPE_LOGICAL   =  1;
constexpr std::uint16_t ADT_TYPE_DATE      =  3;  // 4-byte uint32 LE JDN
constexpr std::uint16_t ADT_TYPE_CHAR      =  4;
constexpr std::uint16_t ADT_TYPE_MEMO      =  5;
constexpr std::uint16_t ADT_TYPE_BINARY    =  6;
constexpr std::uint16_t ADT_TYPE_DOUBLE    = 10;  // 8-byte IEEE754 LE
constexpr std::uint16_t ADT_TYPE_INTEGER   = 11;  // 4-byte int32 LE
constexpr std::uint16_t ADT_TYPE_SHORTINT  = 12;  // 2-byte int16 LE
constexpr std::uint16_t ADT_TYPE_TIME      = 13;  // 4-byte uint32 LE ms
constexpr std::uint16_t ADT_TYPE_TIMESTAMP = 14;  // 8-byte (4B JDN + 4B ms)
constexpr std::uint16_t ADT_TYPE_AUTOINC   = 15;  // 4-byte uint32 LE
constexpr std::uint16_t ADT_TYPE_MONEY     = 18;  // 8-byte IEEE754 LE (same as Double)
constexpr std::uint16_t ADT_TYPE_CICHAR    = 20;  // case-insensitive char (different key enc)
constexpr std::uint16_t ADT_TYPE_ROWVERSION= 21;  // (unconfirmed)
constexpr std::uint16_t ADT_TYPE_MODTIME   = 22;  // 8-byte modification timestamp

// Read-only ADI index.  Each instance represents one tag (one indexed field).
// Multi-tag discovery:  AdiIndex::list_tags(adi_path) → field names
//                       AdiIndex::open_named(adi_path, mode, field_name) → opens one tag
class AdiIndex final : public IIndex {
public:
    using Page = std::array<std::uint8_t, ADI_PAGE_SIZE>;

    // IIndex
    util::Result<void> open(const std::string& path, IndexOpenMode mode) override;

    std::string    name()       const override { return tag_name_; }
    std::string    expression() const override { return tag_name_; }
    bool           descending() const override { return false; }
    bool           unique()     const override { return false; }
    std::uint16_t  key_length() const override { return 8; }

    util::Result<SeekOutcome> seek_first()                           override;
    util::Result<SeekOutcome> seek_last()                            override;
    util::Result<SeekOutcome> seek_key(const std::string& key,
                                       bool soft)                    override;
    util::Result<SeekOutcome> next()                                 override;
    util::Result<SeekOutcome> prev()                                 override;
    std::string current_key() const override { return current_key_; }

    util::Result<void> insert(std::uint32_t, const std::string&) override {
        return util::Error{5000, 0, "ADI index is read-only", ""};
    }
    util::Result<void> erase(std::uint32_t, const std::string&) override {
        return util::Error{5000, 0, "ADI index is read-only", ""};
    }
    util::Result<void> flush() override { return {}; }

    // Multi-tag API (mirrors CdxIndex)
    static util::Result<std::vector<std::string>>
        list_tags(const std::string& adi_path);

    util::Result<void> open_named(const std::string& adi_path,
                                  IndexOpenMode       mode,
                                  const std::string&  field_name);

private:
    // Read a 512-byte page from the ADI file into buf
    util::Result<void> read_adi_page_(std::uint32_t page_no, Page& buf);

    // Load the dense leaf at page_no into cur_page_ and update cursor metadata
    util::Result<void> load_dense_leaf_(std::uint32_t page_no);

    // Navigate to the first (leftmost-most) entry of the B-tree
    util::Result<SeekOutcome> navigate_leftmost_();

    // Navigate to the last (rightmost) entry of the B-tree
    util::Result<SeekOutcome> navigate_rightmost_();

    // Build a SeekOutcome from the current cursor position
    SeekOutcome make_positioned_() const;

    // Update cur_recno_ and current_key_ from cur_page_ at cur_idx_
    util::Result<void> refresh_current_();

    // Compute an 8-byte sign-flipped BE float64 key from an ADT record
    util::Result<std::string> key_for_recno_(std::uint32_t recno);

    // Compare a candidate key (from ADT) against target (8-byte ADI key)
    // Returns negative / 0 / positive
    static int compare_keys_(const std::string& a, const std::string& b) noexcept;

    // ADI file + ADT companion file
    platform::File  adi_file_;
    platform::File  adt_file_;
    std::string     adi_path_;

    // Tag metadata
    std::string     tag_name_;        // ADT field name, e.g. "Date"
    std::uint32_t   root_page_  = 0;
    std::uint16_t   adt_type_   = 0;  // raw ADT type code
    std::uint16_t   fld_offset_ = 0;  // field byte offset in ADT record
    std::uint16_t   fld_length_ = 0;  // field storage length in bytes

    // ADT layout for record reads
    std::uint32_t   adt_hdr_len_ = 0;
    std::uint32_t   adt_rec_len_ = 0;

    // Dense-leaf cursor
    std::uint32_t   entry_size_ = 3;  // dense_entry_size(fld_length_), set in open/open_named
    std::uint32_t   cur_pg_    = ADI_INVALID_PAGE;
    std::int32_t    cur_idx_   = -1;
    std::uint16_t   cur_cnt_   = 0;
    std::uint32_t   cur_lsib_  = ADI_INVALID_PAGE;
    std::uint32_t   cur_rsib_  = ADI_INVALID_PAGE;
    std::uint32_t   cur_recno_ = 0;
    std::string     current_key_;
    Page            cur_page_{};
};

} // namespace openads::drivers::adi
