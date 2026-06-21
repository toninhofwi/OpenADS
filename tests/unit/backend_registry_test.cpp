#include "doctest.h"
#include "abi/backend_table_ops.h"
#include "abi/backend_registry.h"
#include "session/handle_registry.h"

using namespace openads::abi;
using openads::session::HandleKind;

static UNSIGNED32 fake_goto_top(ADSHANDLE) { return 4242; }

TEST_CASE("ops_for_kind returns registered ops, null for unregistered kinds") {
    static const BackendTableOps ops = [] {
        BackendTableOps o{};
        o.goto_top = &fake_goto_top;
        return o;
    }();
    // Snapshot the real registration so this test does not leave the fake ops
    // in the process-global table for later tests in the same binary.
    const BackendTableOps* saved = ops_for_kind(HandleKind::SqliteTable);
    register_backend_table_ops(HandleKind::SqliteTable, &ops);
    CHECK(ops_for_kind(HandleKind::SqliteTable) == &ops);
    CHECK(ops_for_kind(HandleKind::SqliteTable)->goto_top(0) == 4242u);
    CHECK(ops_for_kind(HandleKind::Table) == nullptr);   // native: never registered
    CHECK(ops_for_kind(HandleKind::None) == nullptr);
    register_backend_table_ops(HandleKind::SqliteTable, saved);  // restore
}
