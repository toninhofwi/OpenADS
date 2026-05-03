#pragma once

#include "drivers/dbf_common.h"
#include "platform/file.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers {

enum class IndexOpenMode { ReadOnly, Shared, Exclusive };

enum class SeekHit { Exact, AfterKey, BeforeBegin, AfterEnd };

struct SeekOutcome {
    SeekHit       hit          = SeekHit::AfterEnd;
    std::uint32_t recno        = 0;
    bool          positioned   = false;
};

class IIndex {
public:
    virtual ~IIndex() = default;

    virtual util::Result<void>
        open(const std::string& path, IndexOpenMode mode) = 0;

    virtual std::string name()       const = 0;
    virtual std::string expression() const = 0;
    virtual bool        descending() const = 0;
    virtual bool        unique()     const = 0;
    virtual std::uint16_t key_length() const = 0;

    virtual util::Result<SeekOutcome> seek_first()   = 0;
    virtual util::Result<SeekOutcome> seek_last()    = 0;
    virtual util::Result<SeekOutcome>
        seek_key(const std::string& key, bool soft) = 0;
    virtual util::Result<SeekOutcome> next()         = 0;
    virtual util::Result<SeekOutcome> prev()         = 0;

    virtual std::string current_key() const = 0;

    virtual util::Result<void> insert(std::uint32_t recno,
                                      const std::string& key) = 0;
    virtual util::Result<void> erase (std::uint32_t recno,
                                      const std::string& key) = 0;
    virtual util::Result<void> flush() = 0;
};

} // namespace openads::drivers
