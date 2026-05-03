#include "engine/data_dict.h"

#include "platform/file.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace openads::engine {

namespace {

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
    const char* magic = "# OpenADS Data Dictionary v0\n";
    auto wrote = file.write_at(0, magic, std::strlen(magic));
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    DataDict dd;
    dd.path_ = path;
    return dd;
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
    std::istringstream ss(buf);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        // Expect "TABLE <alias>=<relative_path>".
        if (line.compare(0, 6, "TABLE ") == 0) {
            std::string body = line.substr(6);
            auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            std::string alias = trim(body.substr(0, eq));
            std::string path  = trim(body.substr(eq + 1));
            if (!alias.empty() && !path.empty()) {
                tables_[alias] = path;
            }
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

util::Result<void> DataDict::save() {
    auto fres = platform::File::open(path_, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    auto file = std::move(fres).value();

    std::string out = "# OpenADS Data Dictionary v0\n";
    // Sort aliases for deterministic on-disk content.
    std::vector<std::string> aliases;
    aliases.reserve(tables_.size());
    for (const auto& [k, _] : tables_) aliases.push_back(k);
    std::sort(aliases.begin(), aliases.end());
    for (const auto& a : aliases) {
        out += "TABLE ";
        out += a;
        out += "=";
        out += tables_.at(a);
        out += "\n";
    }
    auto wrote = file.write_at(0, out.data(), out.size());
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();
    return {};
}

} // namespace openads::engine
