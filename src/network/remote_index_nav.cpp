#include "network/remote_index_nav.h"

#include "openads/error.h"

namespace openads::network {

void remote_index_nav_preamble(RemoteIndex* ri) {
    if (ri == nullptr || ri->parent == nullptr) return;
    ri->parent->found_cached  = true;
    ri->parent->current_found = false;
    ri->parent->row_valid     = false;
    ri->parent->prefetch_queue.clear();
    ri->parent->prefetch_consumed = 0;
}

util::Result<void> remote_activate_index(RemoteIndex* ri) {
    if (ri == nullptr || ri->parent == nullptr || ri->conn == nullptr) {
        return util::Error{
            openads::AE_INTERNAL_ERROR, 0,
            "remote index: missing parent or connection", ""};
    }
    if (ri->parent->active_index_id != ri->id) {
        auto r = ri->conn->set_order(ri->parent->id, ri->id);
        if (!r) return r.error();
        ri->parent->active_index_id = ri->id;
    }
    return {};
}

util::Result<void> remote_index_goto_top(RemoteIndex* ri) {
    remote_index_nav_preamble(ri);
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    return ri->conn->goto_top(ri->parent);
}

util::Result<void> remote_index_goto_bottom(RemoteIndex* ri) {
    remote_index_nav_preamble(ri);
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    return ri->conn->goto_bottom(ri->parent);
}

util::Result<void> remote_index_skip(RemoteIndex* ri, std::int32_t rows) {
    remote_index_nav_preamble(ri);
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    return ri->conn->skip(ri->parent, rows);
}

} // namespace openads::network