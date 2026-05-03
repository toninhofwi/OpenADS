#pragma once

#include "engine/table.h"

#include <optional>

namespace openads::engine {

// Forward iteration view of a Table. The Cursor borrows the Table; the
// caller owns the Table lifetime. `next()` returns a non-owning pointer
// to the same Table positioned on the next live record, or std::nullopt
// at EOF.
class Cursor {
public:
    explicit Cursor(Table& t) noexcept : table_(&t) {}

    std::optional<Table*> next();

private:
    Table* table_;
    bool   started_ = false;
};

} // namespace openads::engine
