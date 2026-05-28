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
    const std::unordered_map<std::string, std::string>&
        tables() const noexcept { return tables_; }

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
    const std::unordered_set<std::string>& groups_of(const std::string& user) const;
    const std::unordered_set<std::string>& users() const noexcept { return users_; }
    const std::unordered_map<std::string, std::unordered_set<std::string>>&
        memberships() const noexcept { return memberships_; }

    // ---- GROUP (M-DD-SQL) ------------------------------------------------
    util::Result<void> create_group(const std::string& group);
    util::Result<void> delete_group(const std::string& group);
    bool has_group(const std::string& group) const noexcept {
        return groups_.find(group) != groups_.end();
    }
    const std::unordered_set<std::string>& groups() const noexcept { return groups_; }

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
        std::string parent_tag;
        std::string child_tag;
        std::string update_opt;
        std::string delete_opt;
        std::string fail_table;
    };
    util::Result<void> create_ri(const RiEntry& e);
    util::Result<void> remove_ri(const std::string& name);
    const std::unordered_map<std::string, RiEntry>&
        ri() const noexcept { return ri_; }
    std::unordered_map<std::string, RiEntry>&
        ri()       noexcept { return ri_; }

    // ---- DB / user properties (M10.1) ----------------------------------
    // ---- Field properties (M-DD-FIELD) ----------------------------------
    // Stored props only (required, default, rule, msg, comment).
    // Structural props (name/type/length/decimals) are read live from
    // the table file in AdsDDGetFieldProperty.
    util::Result<void> set_field_property(const std::string& table,
                                           const std::string& field,
                                           const std::string& key,
                                           const std::string& value);
    std::string get_field_property(const std::string& table,
                                    const std::string& field,
                                    const std::string& key) const;

    // ---- Triggers (M-DD-TRIGGER) ----------------------------------------
    struct TriggerEntry {
        std::string   name;
        std::string   table_alias;
        std::uint32_t event_mask = 0;  // ADS_BEFORE/AFTER_INSERT/UPDATE/DELETE bits
        std::string   container;
        std::string   procedure;
        std::uint32_t priority = 0;
        bool          enabled  = true;
        std::string   comment;
    };
    util::Result<void> create_trigger(const TriggerEntry& e);
    util::Result<void> drop_trigger  (const std::string& name);
    bool has_trigger(const std::string& name) const noexcept {
        return triggers_.find(name) != triggers_.end();
    }
    const std::unordered_map<std::string, TriggerEntry>&
        triggers() const noexcept { return triggers_; }
    std::unordered_map<std::string, TriggerEntry>&
        triggers()       noexcept { return triggers_; }

    // ---- Stored procedures (M-DD-PROC) ----------------------------------
    struct ProcEntry {
        std::string name;
        std::string container;
        std::string procedure;
        std::string input_params;
        std::string output_params;
        std::string comment;
    };
    util::Result<void> create_proc(const ProcEntry& e);
    util::Result<void> drop_proc  (const std::string& name);
    bool has_proc(const std::string& name) const noexcept {
        return procs_.find(name) != procs_.end();
    }
    const std::unordered_map<std::string, ProcEntry>&
        procs() const noexcept { return procs_; }
    std::unordered_map<std::string, ProcEntry>&
        procs()       noexcept { return procs_; }

    // ---- Views (M-DD-VIEW) ----------------------------------------------
    struct ViewEntry {
        std::string name;
        std::string sql;
        std::string comment;
    };
    util::Result<void> create_view(const ViewEntry& e);
    util::Result<void> drop_view  (const std::string& name);
    bool has_view(const std::string& name) const noexcept {
        return views_.find(name) != views_.end();
    }
    const std::unordered_map<std::string, ViewEntry>&
        views() const noexcept { return views_; }
    std::unordered_map<std::string, ViewEntry>&
        views()       noexcept { return views_; }

    // ---- Table permissions (M-ACL) ----------------------------------------
    // level: 0=none, 1=read, 2=write, 3=delete, 4=full.
    // user_or_group may be a user name or a group name.
    util::Result<void> set_table_permission(const std::string& table,
                                             const std::string& user_or_group,
                                             int level);
    // Effective level for username on table: max of direct user perm and
    // any group memberships. Returns 4 (full) if no ACL is defined for table.
    int get_effective_permission(const std::string& username,
                                  const std::string& table) const;
    bool has_table_acl(const std::string& table) const noexcept {
        return table_perms_.find(table) != table_perms_.end();
    }
    const std::unordered_map<std::string, std::unordered_map<std::string, int>>&
        table_perms() const noexcept { return table_perms_; }

    // ---- DB / user properties (M10.1) ----------------------------------
    util::Result<void> set_db_property(const std::string& key,
                                       const std::string& value);
    std::string get_db_property(const std::string& key) const;
    const std::unordered_map<std::string, std::string>&
        db_props() const noexcept { return db_props_; }
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
    std::unordered_set<std::string>              groups_;
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
    // table → user_or_group → level (0=none … 4=full)
    std::unordered_map<std::string,
                       std::unordered_map<std::string, int>>
                                                 table_perms_;
    // table_alias → field_name → key → value (stored field props)
    std::unordered_map<std::string,
                       std::unordered_map<std::string,
                                          std::unordered_map<std::string, std::string>>>
                                                 field_props_;
    std::unordered_map<std::string, TriggerEntry> triggers_;
    std::unordered_map<std::string, ProcEntry>    procs_;
    std::unordered_map<std::string, ViewEntry>    views_;

    // Binary format state (populated only when binary_format_ == true).
    bool binary_format_ = false;
    std::string binary_hdr_;            // raw hdr_len bytes, updated in-place
    std::uint32_t binary_hdr_len_ = 0;
    std::uint32_t binary_rec_len_ = 0;
    std::vector<BinaryRecord> binary_recs_;  // all records, active + deleted
};

} // namespace openads::engine
