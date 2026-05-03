#include "engine/cursor.h"

namespace openads::engine {

std::optional<Table*> Cursor::next() {
    if (!started_) {
        started_ = true;
        if (!table_->goto_top()) return std::nullopt;
    } else {
        if (!table_->skip(1))    return std::nullopt;
    }
    if (table_->eof()) return std::nullopt;
    return table_;
}

} // namespace openads::engine
