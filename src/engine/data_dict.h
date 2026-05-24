#pragma once

#include "util/result.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openads::engine {

// Data Dictionary — supports two on-disk formats:
//
//   Text format (OpenADS-native, v1):
//     # OpenADS Data Dictionary v1
//     TABLE     <alias>=<relative_path>
//     INDEX     <table_alias>=<index_path>[\t<comment>]
//     USER      <name>
//     MEMBER    <user>=<group>
//     LINK      <alias>=<path>[\t<user>[\t<pwd>]]
//     RI        <name>=<parent>;<child>;<tag>;<opt_update>;<opt_delete>;<fail_tbl>
//     DBPROP    <key>=<value>
//     USERPROP  <user>;<key>=<value>
//
//   Binary format (ADS proprietary .add):
//     Detected by "ADS Data Dictionary\0" magic at offset 0.
//     Full round-trip: loaded records are preserved verbatim; Table,
//     Index, and User mutations add/delete binary records in-place.

class DataDict {
public:
    DataDict() = default;

    static util::Result<DataDict> open(const std::string& path);

    // Build a fresh empty DD on disk.
    static util::Result<DataDict> create(const std::string& path);

    // ---- TABLE (M6) ------------------------------------------------------
    util::Result<void> add_table(const std::string& alias,
                                 const std::string& relative_path);
    util::Result<void> remove_table(const std::string& alias);

    std::string resolve(const std::string& alias_or_path) const;
    bool has_alias(const std::string& alias) const noexcept {
        return tables_.find(alias) != tables_.end();
    }

    // ---- INDEX (M10.1) ---------------------------------------------------
    struct IndexEntry {
        std::string table_alias;
        std::string index_path;
        std::string comment;
    };
    util::Result<void> add_index_file   (const std::string& table_alias,
                                         const std::string& index_path,
                                         const std::string& comment);
    util::Result<void> remove_index_file(const std::string& table_alias,
                                         const std::string& index_path);
    const std::vector<IndexEntry>& indexes() const noexcept { return indexes_; }

    // ---- USER + MEMBER (M10.1) ------------------------------------------
    util::Result<void> create_user(const std::string& user);
    util::Result<void> delete_user(const std::string& user);
    util::Result<void> add_user_to_group     (const std::string& user,
                                              const std::string& group);
    util::Result<void> remove_user_from_group(const std::string& user,
                                              const std::string& group);
    bool has_user (const std::string& user) const noexcept {
        return users_.find(user) != users_.end();
    }
    bool is_member_of(const std::string& user,
                      const std::string& group) const;

    // ---- LINK (M10.1) ----------------------------------------------------
    struct LinkEntry {
        std::string alias;
        std::string path;
        std::string user;
        std::string pwd;
    };
    util::Result<void> create_link(const std::string& alias,
                                   const std::string& path,
                                   const std::string& user,
                                   const std::string& pwd);
    util::Result<void> drop_link  (const std::string& alias);
    util::Result<void> modify_link(const std::string& alias,
                                   const std::string& path,
                                   const std::string& user,
                                   const std::string& pwd);
    const std::unordered_map<std::string, LinkEntry>&
        links() const noexcept { return links_; }

    // ---- RI rules (M10.1) ----------------------------------------------
    struct RiEntry {
        std::string name;
        std::string parent;
        std::string child;
        std::string tag;
        std::string update_opt;
        std::string delete_opt;
        std::string fail_table;
    };
    util::Result<void> create_ri(const RiEntry& e);
    util::Result<void> remove_ri(const std::string& name);
    const std::unordered_map<std::string, RiEntry>&
        ri() const noexcept { return ri_; }

    // ---- DB / user properties (M10.1) ----------------------------------
    util::Result<void> set_db_property(const std::string& key,
                                       const std::string& value);
    std::string get_db_property(const std::string& key) const;
    util::Result<void> set_user_property(const std::string& user,
                                         const std::string& key,
                                         const std::string& value);
    std::string get_user_property(const std::string& user,
                                  const std::string& key) const;

    util::Result<void> save();

    const std::string& path() const noexcept { return path_; }

private:
    // In-memory representation of one binary .add record.
    struct BinaryRecord {
        bool active = true;
        std::uint32_t obj_id = 0;
        std::uint32_t parent_id = 0;
        std::string obj_type;           // up to 10 chars (trimmed)
        std::string obj_name;           // up to 200 chars (trimmed)
        std::string property;           // raw VarChar bytes (may include \0)
        bool prop_null = true;          // true → stored as 0xFFFF
        std::array<std::uint8_t, 9> more_property{};
        std::uint32_t info1 = 0;
        std::uint32_t info2 = 0;
        std::array<std::uint8_t, 9> comment{};
    };

    util::Result<void> load_();
    util::Result<void> load_add_binary_(const std::string& buf);
    util::Result<void> save_add_binary_();

    std::uint32_t binary_alloc_id_();           // consume + advance next ObjID
    std::uint32_t binary_obj_id_of_(const std::string& obj_type,
                                    const std::string& name) const;
    static std::string serialize_binary_rec_(const BinaryRecord& r,
                                             std::uint32_t rec_len);

    std::string                                  path_;
    std::unordered_map<std::string, std::string> tables_;
    std::vector<IndexEntry>                      indexes_;
    std::unordered_set<std::string>              users_;
    // user → set<group>
    std::unordered_map<std::string,
                       std::unordered_set<std::string>> memberships_;
    std::unordered_map<std::string, LinkEntry>   links_;
    std::unordered_map<std::string, RiEntry>     ri_;
    std::unordered_map<std::string, std::string> db_props_;
    // user → key → value
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>
                                                 user_props_;

    // Binary format state (populated only when binary_format_ == true).
    bool binary_format_ = false;
    std::string binary_hdr_;            // raw hdr_len bytes, updated in-place
    std::uint32_t binary_hdr_len_ = 0;
    std::uint32_t binary_rec_len_ = 0;
    std::vector<BinaryRecord> binary_recs_;  // all records, active + deleted
};

} // namespace openads::engine
