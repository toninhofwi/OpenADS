#pragma once

#include "util/result.h"

#include <string>
#include <unordered_map>

namespace openads::engine {

// Minimal Data Dictionary. The OpenADS-native format is a UTF-8 text
// file with one record per line:
//
//   TABLE <alias>=<relative_path>
//
// The original ADS `.add` binary format is proprietary and is not
// implemented here; OpenADS uses its own clean-room text format and
// only round-trips with itself. A future milestone may add a
// compatible binary writer when a clean-room specification is
// available.

class DataDict {
public:
    DataDict() = default;

    static util::Result<DataDict> open(const std::string& path);

    // Build a fresh empty DD on disk.
    static util::Result<DataDict> create(const std::string& path);

    util::Result<void> add_table(const std::string& alias,
                                 const std::string& relative_path);
    util::Result<void> remove_table(const std::string& alias);

    // Resolve an alias to its relative path. Returns the input when no
    // alias of that name is known (so callers can pass either an alias
    // or a literal path).
    std::string resolve(const std::string& alias_or_path) const;

    bool has_alias(const std::string& alias) const noexcept {
        return tables_.find(alias) != tables_.end();
    }

    util::Result<void> save();

    const std::string& path() const noexcept { return path_; }

private:
    util::Result<void> load_();

    std::string                                 path_;
    std::unordered_map<std::string, std::string> tables_;
};

} // namespace openads::engine
