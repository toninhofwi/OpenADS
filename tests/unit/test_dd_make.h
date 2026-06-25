#pragma once
// Shared test helper: create an OpenADS .add file from a terse text body.
// Used by tests that were previously writing the old text-format DD directly.
//
// Supported line types (one per text line, blank/# lines ignored):
//   TABLE   alias=relative_path
//   USER    username
//   USERPROP username;key=value
//   GROUP   groupname
//   MEMBER  username=groupname
//   DBPROP  key=value
//   TABLEPERM  table;grantee=level       (level: 0-4)
//   RI      name=parent;child;parent_tag[;child_tag];upd;del[;fail]
//   TRIGGER name=table;event;timing;priority[;enabled];container;proc[;comment][;options]
//   PROC    name=container;proc_fn;input;output[;comment]
//   VIEW    name=comment;sql

#include "engine/data_dict.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace openads_test {

namespace detail {

static std::vector<std::string> split(const std::string& s, char delim,
                                       std::size_t max_parts = std::string::npos) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < s.size()) {
        if (max_parts != std::string::npos && out.size() + 1 >= max_parts) {
            out.push_back(s.substr(start));
            break;
        }
        auto pos = s.find(delim, start);
        if (pos == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

static std::string field(const std::vector<std::string>& v, std::size_t i,
                          const std::string& def = {}) {
    return i < v.size() ? v[i] : def;
}

} // namespace detail

inline std::filesystem::path make_dd(const std::filesystem::path& path,
                                      const std::string& body) {
    using namespace openads::engine;
    namespace fs = std::filesystem;

    auto cr = DataDict::create(path.string());
    if (!cr.has_value()) return {};
    DataDict dd = std::move(cr).value();

    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        auto keyword = line.substr(0, sp);
        auto rest    = line.substr(sp + 1);

        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            // No '=' — keyword with just a name (USER, GROUP).
            if (keyword == "USER")  { dd.create_user(rest); continue; }
            if (keyword == "GROUP") { dd.create_group(rest); continue; }
            continue;
        }

        std::string name  = rest.substr(0, eq);
        std::string value = rest.substr(eq + 1);

        if (keyword == "TABLE") {
            dd.add_table(name, value);

        } else if (keyword == "USER") {
            dd.create_user(name);

        } else if (keyword == "GROUP") {
            dd.create_group(name);

        } else if (keyword == "MEMBER") {
            dd.add_user_to_group(name, value);

        } else if (keyword == "DBPROP") {
            // key=value
            dd.set_db_property(name, value);

        } else if (keyword == "USERPROP") {
            // name here is "username;key", value is the property value
            auto sc = name.find(';');
            if (sc != std::string::npos) {
                std::string user = name.substr(0, sc);
                std::string key  = name.substr(sc + 1);
                dd.set_user_property(user, key, value);
            }

        } else if (keyword == "TABLEPERM") {
            // name is "table;grantee", value is level
            auto sc = name.find(';');
            if (sc != std::string::npos) {
                std::string tbl     = name.substr(0, sc);
                std::string grantee = name.substr(sc + 1);
                int level = value.empty() ? 0 : std::stoi(value);
                dd.set_table_permission(tbl, grantee, level);
            }

        } else if (keyword == "RI") {
            // name=parent;child;parent_tag[;child_tag];upd;del[;fail]
            auto parts = detail::split(value, ';');
            DataDict::RiEntry e;
            e.name = name;
            e.parent     = detail::field(parts, 0);
            e.child      = detail::field(parts, 1);
            if (parts.size() >= 7) {
                // Long format: parent;child;parent_tag;child_tag;upd;del;fail
                e.parent_tag = detail::field(parts, 2);
                e.child_tag  = detail::field(parts, 3);
                e.update_opt = detail::field(parts, 4);
                e.delete_opt = detail::field(parts, 5);
                e.fail_table = detail::field(parts, 6);
            } else {
                // Short format: parent;child;tag;upd;del[;fail]
                e.parent_tag = detail::field(parts, 2);
                e.update_opt = detail::field(parts, 3);
                e.delete_opt = detail::field(parts, 4);
                e.fail_table = detail::field(parts, 5);
            }
            dd.create_ri(e);

        } else if (keyword == "TRIGGER") {
            // name=table;event;timing;priority[;enabled];container;proc[;comment][;options]
            auto parts = detail::split(value, ';');
            DataDict::TriggerEntry e;
            e.name        = name;
            e.table_alias = detail::field(parts, 0);
            try { e.event_mask = std::stoul(detail::field(parts, 1, "0")); } catch (...) {}
            try { e.timing     = std::stoul(detail::field(parts, 2, "0")); } catch (...) {}
            if (parts.size() >= 9) {
                // Long format: table;event;timing;priority;enabled;container;proc;comment;options
                try { e.priority = std::stoul(detail::field(parts, 3, "1")); } catch (...) {}
                std::string en = detail::field(parts, 4, "1");
                e.enabled   = (en != "0" && en != "false");
                e.container = detail::field(parts, 5);
                e.procedure = detail::field(parts, 6);
                e.comment   = detail::field(parts, 7);
                try { e.options = std::stoul(detail::field(parts, 8, "3")); } catch (...) {}
            } else {
                // Short format: table;event;timing;priority;container;proc[;...]
                try { e.priority = std::stoul(detail::field(parts, 3, "1")); } catch (...) {}
                e.enabled   = true;
                e.container = detail::field(parts, 4);
                e.procedure = detail::field(parts, 5);
                e.comment   = detail::field(parts, 6);
                try { e.options = std::stoul(detail::field(parts, 7, "3")); } catch (...) {}
            }
            dd.create_trigger(e);

        } else if (keyword == "PROC") {
            // name=container;proc_fn;input;output[;comment]
            auto parts = detail::split(value, ';');
            DataDict::ProcEntry e;
            e.name          = name;
            e.container     = detail::field(parts, 0);
            e.procedure     = detail::field(parts, 1);
            e.input_params  = detail::field(parts, 2);
            e.output_params = detail::field(parts, 3);
            e.comment       = detail::field(parts, 4);
            dd.create_proc(e);

        } else if (keyword == "VIEW") {
            // name=comment;sql  (comment before first ';', sql is the rest)
            auto sc = value.find(';');
            DataDict::ViewEntry e;
            e.name    = name;
            e.comment = (sc != std::string::npos) ? value.substr(0, sc) : std::string{};
            e.sql     = (sc != std::string::npos) ? value.substr(sc + 1) : value;
            dd.create_view(e);
        }
    }
    return path;
}

// Overload for write_dd(path, body) callers using string path.
inline std::filesystem::path make_dd(const std::string& path, const std::string& body) {
    return make_dd(std::filesystem::path(path), body);
}

} // namespace openads_test
