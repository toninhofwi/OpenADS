#include "doctest.h"

#include "openads/ace.h"
#include "sql_backend/firebird_connection.h"
#include "sql_backend/firebird_uri.h"

#include <cstdlib>
#include <string>

using openads::sql_backend::FirebirdConnection;
using openads::sql_backend::FirebirdTable;
using openads::sql_backend::FirebirdUri;
using openads::sql_backend::IndexExprKind;

namespace {

// Live embedded-Firebird fixture: OPENADS_TEST_FIREBIRD_DB points at a
// `.fdb` already seeded with a `clientes` table —
//   (1,'Ana',10.5), (2,'Bob',NULL), (3,'Cid',0.0)
// ID INTEGER PRIMARY KEY, NOME VARCHAR(64), SALDO DOUBLE PRECISION.
// The run script (tools/scripts/run_firebird_native_tests.ps1) copies the
// fixture, sets FIREBIRD + PATH for the embedded engine, and exports the
// variable. When unset the live test is skipped (no isc_* call is made, so
// the delay-loaded client DLL is never required).
const char* fixture_db() {
    const char* v = std::getenv("OPENADS_TEST_FIREBIRD_DB");
    return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

FirebirdUri embedded_uri(const char* path) {
    FirebirdUri u;
    u.embedded = true;
    u.dbpath   = path;
    u.user     = "SYSDBA";
    u.charset  = "UTF8";
    return u;
}

std::string read(const FirebirdConnection& c, FirebirdTable* t, const char* col) {
    std::string buf;
    bool is_null = false;
    REQUIRE(c.read_field(t, col, buf, is_null));
    return buf;
}

bool read_null(const FirebirdConnection& c, FirebirdTable* t, const char* col) {
    std::string buf;
    bool is_null = false;
    REQUIRE(c.read_field(t, col, buf, is_null));
    return is_null;
}

} // namespace

TEST_CASE("firebird native: read-only navigation over the embedded fixture") {
    const char* db = fixture_db();
    if (db == nullptr) {
        MESSAGE("OPENADS_TEST_FIREBIRD_DB not set; skipping live Firebird test");
        return;
    }

    auto cr = FirebirdConnection::open(embedded_uri(db));
    REQUIRE(cr);
    FirebirdConnection conn = std::move(cr).value();
    REQUIRE(conn.valid());

    auto tr = conn.open_table("clientes");
    REQUIRE(tr);
    auto tbl = std::move(tr).value();

    auto fields = conn.describe_table(tbl.get());
    REQUIRE(fields);
    CHECK(fields.value().size() == 3);

    auto rc = conn.record_count(tbl.get());
    REQUIRE(rc);
    CHECK(rc.value() == 3);

    REQUIRE(conn.goto_top(tbl.get()));
    auto bof = conn.at_bof(tbl.get());
    REQUIRE(bof);
    CHECK_FALSE(bof.value());
    CHECK(read(conn, tbl.get(), "ID") == "1");
    CHECK(read(conn, tbl.get(), "NOME") == "Ana");

    REQUIRE(conn.skip(tbl.get(), 1));               // Bob
    CHECK(read(conn, tbl.get(), "NOME") == "Bob");
    CHECK(read_null(conn, tbl.get(), "SALDO"));     // Bob's SALDO is NULL

    REQUIRE(conn.skip(tbl.get(), 1));               // Cid
    CHECK(read(conn, tbl.get(), "NOME") == "Cid");

    REQUIRE(conn.skip(tbl.get(), 1));               // past the end
    auto eof = conn.at_eof(tbl.get());
    REQUIRE(eof);
    CHECK(eof.value());

    REQUIRE(conn.goto_bottom(tbl.get()));
    CHECK(read(conn, tbl.get(), "NOME") == "Cid");
}

TEST_CASE("firebird native: seek by primary key") {
    const char* db = fixture_db();
    if (db == nullptr) {
        MESSAGE("OPENADS_TEST_FIREBIRD_DB not set; skipping live Firebird test");
        return;
    }
    auto cr = FirebirdConnection::open(embedded_uri(db));
    REQUIRE(cr);
    FirebirdConnection conn = std::move(cr).value();
    auto tbl = std::move(conn.open_table("clientes")).value();

    auto found = conn.seek_index(tbl.get(), "ID", IndexExprKind::Column,
                                 "2", /*soft=*/false, /*last_key=*/false);
    REQUIRE(found);
    CHECK(found.value());
    CHECK(read(conn, tbl.get(), "NOME") == "Bob");

    auto miss = conn.seek_index(tbl.get(), "ID", IndexExprKind::Column,
                                "999", /*soft=*/false, /*last_key=*/false);
    REQUIRE(miss);
    CHECK_FALSE(miss.value());
}

TEST_CASE("firebird native: write append / update / delete round-trip") {
    const char* db = fixture_db();
    if (db == nullptr) {
        MESSAGE("OPENADS_TEST_FIREBIRD_DB not set; skipping live Firebird test");
        return;
    }
    auto cr = FirebirdConnection::open(embedded_uri(db));
    REQUIRE(cr);
    FirebirdConnection conn = std::move(cr).value();
    auto tbl = std::move(conn.open_table("clientes")).value();

    // APPEND a new row (id 4) — leaves the fixture untouched once deleted.
    REQUIRE(conn.append_blank(tbl.get()));
    REQUIRE(conn.set_field(tbl.get(), "ID", "4"));
    REQUIRE(conn.set_field(tbl.get(), "NOME", "Dan"));
    REQUIRE(conn.set_field(tbl.get(), "SALDO", "99.9"));
    REQUIRE(conn.flush_record(tbl.get()));

    auto rc = conn.record_count(tbl.get());
    REQUIRE(rc);
    CHECK(rc.value() == 4);

    auto found = conn.seek_index(tbl.get(), "ID", IndexExprKind::Column,
                                 "4", false, false);
    REQUIRE(found);
    CHECK(found.value());
    CHECK(read(conn, tbl.get(), "NOME") == "Dan");

    // UPDATE the row.
    REQUIRE(conn.set_field(tbl.get(), "NOME", "DanX"));
    REQUIRE(conn.flush_record(tbl.get()));
    REQUIRE(conn.seek_index(tbl.get(), "ID", IndexExprKind::Column, "4", false, false));
    CHECK(read(conn, tbl.get(), "NOME") == "DanX");

    // DELETE the row — restores the original 3-row fixture.
    REQUIRE(conn.seek_index(tbl.get(), "ID", IndexExprKind::Column, "4", false, false));
    REQUIRE(conn.delete_record(tbl.get()));

    auto rc2 = conn.record_count(tbl.get());
    REQUIRE(rc2);
    CHECK(rc2.value() == 3);

    auto gone = conn.seek_index(tbl.get(), "ID", IndexExprKind::Column, "4", false, false);
    REQUIRE(gone);
    CHECK_FALSE(gone.value());
}
