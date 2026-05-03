#pragma once

#include "drivers/index_trait.h"

namespace openads::drivers::cdx {

class CdxIndex final : public IIndex {
public:
    util::Result<void> open(const std::string&, IndexOpenMode) override
    { return util::Error{5004, 0, "CdxIndex not yet implemented", ""}; }

    std::string name()       const override { return {}; }
    std::string expression() const override { return {}; }
    bool        descending() const override { return false; }
    bool        unique()     const override { return false; }
    std::uint16_t key_length() const override { return 0; }

    util::Result<SeekOutcome> seek_first() override { return SeekOutcome{}; }
    util::Result<SeekOutcome> seek_last()  override { return SeekOutcome{}; }
    util::Result<SeekOutcome> seek_key(const std::string&, bool) override { return SeekOutcome{}; }
    util::Result<SeekOutcome> next()       override { return SeekOutcome{}; }
    util::Result<SeekOutcome> prev()       override { return SeekOutcome{}; }
    std::string current_key() const override { return {}; }

    util::Result<void> insert(std::uint32_t, const std::string&) override
    { return util::Error{5004, 0, "CdxIndex insert not yet implemented", ""}; }
    util::Result<void> erase (std::uint32_t, const std::string&) override
    { return util::Error{5004, 0, "CdxIndex erase not yet implemented", ""}; }
    util::Result<void> flush() override { return {}; }
};

} // namespace openads::drivers::cdx
