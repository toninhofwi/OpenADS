#include "engine/data_dict.h"

#include "platform/file.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace openads::engine {

namespace {

static inline uint32_t le32(const std::string& b, std::size_t off) {
    return static_cast<uint8_t>(b[off])
         | (static_cast<uint8_t>(b[off+1]) << 8)
         | (static_cast<uint8_t>(b[off+2]) << 16)
         | (static_cast<uint8_t>(b[off+3]) << 24);
}

static inline uint16_t le16(const std::string& b, std::size_t off) {
    return static_cast<uint8_t>(b[off])
         | (static_cast<uint8_t>(b[off+1]) << 8);
}

std::string trim(std::string s) {
    auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace

util::Result<DataDict> DataDict::open(const std::string& path) {
    DataDict dd;
    dd.path_ = path;
    if (auto r = dd.load_(); !r) return r.error();
    return dd;
}

util::Result<DataDict> DataDict::create(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();
    const char* magic = "# OpenADS Data Dictionary v1\n";
    auto wrote = file.write_at(0, magic, std::strlen(magic));
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    DataDict dd;
    dd.path_ = path;
    return dd;
}

namespace {
std::vector<std::string> split_tabs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}
}  // namespace

util::Result<void> DataDict::load_add_binary_(const std::string& buf) {
    // ADS Data Dictionary binary format:
    //   sig "ADS Data Dictionary\0" (20 bytes)
    //   hdr_len uint32 LE at 0x20  — total header including field descriptors
    //   rec_len  uint32 LE at 0x24 — bytes per record
    //   Records start at hdr_len; each record:
    //     [0]      status 0x04=active, 0x05=deleted
    //     [1..4]   null bitmap (4 bytes)
    //     [13..22] Object Type  — CHAR(10) space-padded
    //     [23..222]Object Name  — CHAR(200) space-padded
    //     [223..]  Property     — uint16 LE length prefix + data
    if (buf.size() < 40)
        return util::Error{5000, 0, "ADD file too small", path_};

    uint32_t hdr_len = le32(buf, 0x20);
    uint32_t rec_len  = le32(buf, 0x24);

    if (rec_len == 0 || hdr_len > buf.size())
        return util::Error{5000, 0, "ADD header corrupt", path_};

    std::size_t total = (buf.size() - hdr_len) / rec_len;
    for (std::size_t i = 0; i < total; ++i) {
        std::size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;

        if (static_cast<uint8_t>(buf[base]) != 0x04) continue;  // deleted/free

        // Object Type: CHAR(10) at offset 13
        std::string obj_type = buf.substr(base + 13, 10);
        {
            auto p = obj_type.find_last_not_of(" \0", std::string::npos, 2);
            obj_type = (p == std::string::npos) ? "" : obj_type.substr(0, p + 1);
        }

        if (obj_type != "Table") continue;

        // Object Name: CHAR(200) at offset 23 — the alias
        std::string alias = buf.substr(base + 23, 200);
        {
            auto p = alias.find_last_not_of(" \0", std::string::npos, 2);
            alias = (p == std::string::npos) ? "" : alias.substr(0, p + 1);
        }
        if (alias.empty()) continue;

        // Property: uint16 LE length at offset 223, then path bytes
        if (base + 223 + 2 > buf.size()) continue;
        uint16_t plen = le16(buf, base + 223);
        if (plen == 0xFFFFu || plen == 0 ||
            base + 225 + plen > buf.size()) continue;

        std::string path = buf.substr(base + 225, plen);
        // Strip embedded null terminator (the length includes it)
        auto nul = path.find('\0');
        if (nul != std::string::npos) path.resize(nul);
        if (path.empty()) continue;

        tables_[alias] = path;
    }
    return {};
}

util::Result<void> DataDict::load_() {
    auto fres = platform::File::open(path_, platform::OpenMode::ReadOnly);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();
    auto sz = file.size();
    if (!sz) return sz.error();
    std::string buf(static_cast<std::size_t>(sz.value()), '\0');
    if (!buf.empty()) {
        auto rd = file.read_at(0, buf.data(), buf.size());
        if (!rd) return rd.error();
    }

    // Detect ADS proprietary binary format by its fixed signature.
    if (buf.size() >= 20 &&
        buf.compare(0, 19, "ADS Data Dictionary") == 0 &&
        static_cast<uint8_t>(buf[19]) == 0x00) {
        return load_add_binary_(buf);
    }

    std::istringstream ss(buf);
    std::string line;
    auto starts_with = [](const std::string& s, const char* k) {
        std::size_t n = std::strlen(k);
        return s.size() >= n && s.compare(0, n, k) == 0;
    };
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (starts_with(line, "TABLE ")) {
            std::string body = line.substr(6);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string path  = trim(body.substr(eq + 1));
            if (!alias.empty() && !path.empty()) tables_[alias] = path;

        } else if (starts_with(line, "INDEX ")) {
            std::string body = line.substr(6);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string rest  = body.substr(eq + 1);
            auto parts = split_tabs(rest);
            if (alias.empty() || parts.empty() || parts[0].empty()) continue;
            IndexEntry e;
            e.table_alias = alias;
            e.index_path  = parts[0];
            if (parts.size() > 1) e.comment = parts[1];
            indexes_.push_back(std::move(e));

        } else if (starts_with(line, "USER ")) {
            std::string name = trim(line.substr(5));
            if (!name.empty()) users_.insert(name);

        } else if (starts_with(line, "MEMBER ")) {
            std::string body = line.substr(7);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string user  = trim(body.substr(0, eq));
            std::string group = trim(body.substr(eq + 1));
            if (!user.empty() && !group.empty()) {
                memberships_[user].insert(group);
            }

        } else if (starts_with(line, "LINK ")) {
            std::string body = line.substr(5);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string rest  = body.substr(eq + 1);
            auto parts = split_tabs(rest);
            if (alias.empty() || parts.empty()) continue;
            LinkEntry e;
            e.alias = alias;
            e.path  = parts[0];
            if (parts.size() > 1) e.user = parts[1];
            if (parts.size() > 2) e.pwd  = parts[2];
            links_[alias] = std::move(e);

        } else if (starts_with(line, "RI ")) {
            std::string body = line.substr(3);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string name = trim(body.substr(0, eq));
            std::string rest = body.substr(eq + 1);
            // parent;child;tag;update_opt;delete_opt;fail_table
            std::vector<std::string> parts;
            std::string cur;
            for (char c : rest) {
                if (c == ';') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            parts.push_back(cur);
            while (parts.size() < 6) parts.emplace_back();
            RiEntry e;
            e.name        = name;
            e.parent      = parts[0];
            e.child       = parts[1];
            e.tag         = parts[2];
            e.update_opt  = parts[3];
            e.delete_opt  = parts[4];
            e.fail_table  = parts[5];
            ri_[name] = std::move(e);

        } else if (starts_with(line, "DBPROP ")) {
            std::string body = line.substr(7);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            db_props_[trim(body.substr(0, eq))] = trim(body.substr(eq + 1));

        } else if (starts_with(line, "USERPROP ")) {
            std::string body = line.substr(9);
            auto sc = body.find(';');
            if (sc == std::string::npos) continue;
            std::string user = trim(body.substr(0, sc));
            std::string rest = body.substr(sc + 1);
            auto eq = rest.find('=');
            if (eq == std::string::npos) continue;
            user_props_[user][trim(rest.substr(0, eq))] =
                trim(rest.substr(eq + 1));
        }
    }
    return {};
}

util::Result<void>
DataDict::add_table(const std::string& alias,
                    const std::string& relative_path) {
    if (alias.empty() || relative_path.empty()) {
        return util::Error{5000, 0, "DD alias / path must be non-empty", ""};
    }
    tables_[alias] = relative_path;
    return save();
}

util::Result<void> DataDict::remove_table(const std::string& alias) {
    tables_.erase(alias);
    return save();
}

std::string DataDict::resolve(const std::string& alias_or_path) const {
    auto it = tables_.find(alias_or_path);
    if (it != tables_.end()) return it->second;
    return alias_or_path;
}

util::Result<void>
DataDict::add_index_file(const std::string& table_alias,
                         const std::string& index_path,
                         const std::string& comment) {
    if (table_alias.empty() || index_path.empty()) {
        return util::Error{5000, 0, "DD index alias / path empty", ""};
    }
    indexes_.push_back({table_alias, index_path, comment});
    return save();
}

util::Result<void>
DataDict::remove_index_file(const std::string& table_alias,
                            const std::string& index_path) {
    auto it = std::remove_if(indexes_.begin(), indexes_.end(),
        [&](const IndexEntry& e) {
            return e.table_alias == table_alias && e.index_path == index_path;
        });
    indexes_.erase(it, indexes_.end());
    return save();
}

util::Result<void> DataDict::create_user(const std::string& user) {
    if (user.empty()) {
        return util::Error{5000, 0, "DD user name empty", ""};
    }
    users_.insert(user);
    return save();
}

util::Result<void> DataDict::delete_user(const std::string& user) {
    users_.erase(user);
    memberships_.erase(user);
    user_props_.erase(user);
    return save();
}

util::Result<void>
DataDict::add_user_to_group(const std::string& user,
                            const std::string& group) {
    if (user.empty() || group.empty()) {
        return util::Error{5000, 0, "DD member user / group empty", ""};
    }
    memberships_[user].insert(group);
    return save();
}

util::Result<void>
DataDict::remove_user_from_group(const std::string& user,
                                 const std::string& group) {
    auto it = memberships_.find(user);
    if (it != memberships_.end()) {
        it->second.erase(group);
        if (it->second.empty()) memberships_.erase(it);
    }
    return save();
}

bool DataDict::is_member_of(const std::string& user,
                            const std::string& group) const {
    auto it = memberships_.find(user);
    if (it == memberships_.end()) return false;
    return it->second.find(group) != it->second.end();
}

util::Result<void>
DataDict::create_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    if (alias.empty() || path.empty()) {
        return util::Error{5000, 0, "DD link alias / path empty", ""};
    }
    links_[alias] = {alias, path, user, pwd};
    return save();
}

util::Result<void> DataDict::drop_link(const std::string& alias) {
    links_.erase(alias);
    return save();
}

util::Result<void>
DataDict::modify_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    auto it = links_.find(alias);
    if (it == links_.end()) {
        return util::Error{5000, 0, "DD link not found", alias};
    }
    if (!path.empty()) it->second.path = path;
    it->second.user = user;
    it->second.pwd  = pwd;
    return save();
}

util::Result<void> DataDict::create_ri(const RiEntry& e) {
    if (e.name.empty()) {
        return util::Error{5000, 0, "DD RI name empty", ""};
    }
    ri_[e.name] = e;
    return save();
}

util::Result<void> DataDict::remove_ri(const std::string& name) {
    ri_.erase(name);
    return save();
}

util::Result<void>
DataDict::set_db_property(const std::string& key, const std::string& value) {
    if (key.empty()) {
        return util::Error{5000, 0, "DD db-property key empty", ""};
    }
    db_props_[key] = value;
    return save();
}

std::string DataDict::get_db_property(const std::string& key) const {
    auto it = db_props_.find(key);
    return it == db_props_.end() ? std::string{} : it->second;
}

util::Result<void>
DataDict::set_user_property(const std::string& user,
                            const std::string& key,
                            const std::string& value) {
    if (user.empty() || key.empty()) {
        return util::Error{5000, 0, "DD user-property user / key empty", ""};
    }
    user_props_[user][key] = value;
    return save();
}

std::string
DataDict::get_user_property(const std::string& user,
                            const std::string& key) const {
    auto u = user_props_.find(user);
    if (u == user_props_.end()) return {};
    auto k = u->second.find(key);
    return k == u->second.end() ? std::string{} : k->second;
}

util::Result<void> DataDict::save() {
    auto fres = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();

    std::string out = "# OpenADS Data Dictionary v1\n";

    auto sorted_keys = [](const auto& m) {
        std::vector<std::string> ks;
        ks.reserve(m.size());
        for (const auto& kv : m) ks.push_back(kv.first);
        std::sort(ks.begin(), ks.end());
        return ks;
    };

    for (auto& a : sorted_keys(tables_)) {
        out += "TABLE " + a + "=" + tables_.at(a) + "\n";
    }
    for (auto& e : indexes_) {
        out += "INDEX " + e.table_alias + "=" + e.index_path;
        if (!e.comment.empty()) { out += "\t"; out += e.comment; }
        out += "\n";
    }
    {
        std::vector<std::string> us(users_.begin(), users_.end());
        std::sort(us.begin(), us.end());
        for (auto& u : us) out += "USER " + u + "\n";
    }
    for (auto& u : sorted_keys(memberships_)) {
        std::vector<std::string> gs(memberships_.at(u).begin(),
                                    memberships_.at(u).end());
        std::sort(gs.begin(), gs.end());
        for (auto& g : gs) out += "MEMBER " + u + "=" + g + "\n";
    }
    for (auto& a : sorted_keys(links_)) {
        const auto& e = links_.at(a);
        out += "LINK " + a + "=" + e.path;
        if (!e.user.empty() || !e.pwd.empty()) {
            out += "\t"; out += e.user;
        }
        if (!e.pwd.empty()) { out += "\t"; out += e.pwd; }
        out += "\n";
    }
    for (auto& n : sorted_keys(ri_)) {
        const auto& e = ri_.at(n);
        out += "RI " + n + "=" + e.parent + ";" + e.child + ";" +
               e.tag + ";" + e.update_opt + ";" + e.delete_opt + ";" +
               e.fail_table + "\n";
    }
    for (auto& k : sorted_keys(db_props_)) {
        out += "DBPROP " + k + "=" + db_props_.at(k) + "\n";
    }
    for (auto& u : sorted_keys(user_props_)) {
        std::vector<std::string> ks;
        for (const auto& [k, _] : user_props_.at(u)) ks.push_back(k);
        std::sort(ks.begin(), ks.end());
        for (auto& k : ks) {
            out += "USERPROP " + u + ";" + k + "=" +
                   user_props_.at(u).at(k) + "\n";
        }
    }

    auto wrote = file.write_at(0, out.data(), out.size());
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    return {};
}

} // namespace openads::engine
