#pragma once

#include "network/client.h"
#include "util/result.h"

#include <cstdint>

namespace openads::network {

// Invalidate parent row cache / prefetch before index-driven navigation.
void remote_index_nav_preamble(RemoteIndex* ri);

// Ensure the parent RemoteTable's active order matches this index tag.
util::Result<void> remote_activate_index(RemoteIndex* ri);

// rddads passes hOrdCurrent (a RemoteIndex ADSHANDLE) to AdsGotoTop /
// AdsGotoBottom / AdsSkip. Route those calls through the parent table
// cursor once the tag above is active.
util::Result<void> remote_index_goto_top(RemoteIndex* ri);
util::Result<void> remote_index_goto_bottom(RemoteIndex* ri);
util::Result<void> remote_index_skip(RemoteIndex* ri, std::int32_t rows);

} // namespace openads::network