#include "doctest.h"
#include "session/handle_registry.h"

using openads::session::HandleRegistry;
using openads::session::HandleKind;

TEST_CASE("kind_of returns the registered kind, None for unknown") {
    HandleRegistry reg;
    int dummy = 0;
    auto h = reg.register_object(HandleKind::SqliteTable, &dummy);
    CHECK(reg.kind_of(h) == HandleKind::SqliteTable);
    CHECK(reg.kind_of(h + 999) == HandleKind::None);
    reg.release(h);
    CHECK(reg.kind_of(h) == HandleKind::None);
}
