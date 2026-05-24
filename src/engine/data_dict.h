#pragma once

#include "util/result.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openads::engine {

// Data Dictionary — clean-room OpenADS-native text format. Round-
// trips with itself only; the proprietary ADS `.add` binary stays
// unimplemented until a clean-room specification is available.
//
// Format v1 (M10.1) extends v0's `TABLE alias=path` rows with the
// per-entity rows below. Unknown rows on load are preserved when
// the format version changes; comments (`# ...`) round-trip too.
//
//   # OpenADS Data Dictionary v1
//   TABLE     <alias>=<relative_path>
//   INDEX     <table_alias>=<index_path>[\t<comment>]
//   USER      <name>
//   MEMBER    <user>=<group>
//   LINK      <alias>=<path>[\t<user>[\t<pwd>]]
//   RI        <name>=<parent>;<child>;<tag>;<opt_update>;<opt_delete>;<fail_tbl>
//   DBPROP    <key>=<value>
//   USERPROP  <user>;<key>=<value>

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
    util::Result<void> load_();
    util::Result<void> load_add_binary_(const std::string& buf);

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
};

} // namespace openads::engine
