#include "doctest.h"

#include "sql_backend/firebird_uri.h"

using openads::sql_backend::FirebirdUri;
using openads::sql_backend::parse_firebird_uri;

TEST_CASE("firebird URI: embedded triple-slash form (no server)") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri("firebird:///C:/data/app.fdb", u));
    CHECK(u.embedded);
    CHECK(u.host.empty());
    CHECK(u.port == 0);
    CHECK(u.dbpath == "C:/data/app.fdb");
    // Embedded attach string is the bare path.
    CHECK(u.attach_string() == "C:/data/app.fdb");

    FirebirdUri p;
    REQUIRE(parse_firebird_uri("firebird:///var/lib/firebird/app.fdb", p));
    CHECK(p.embedded);
    CHECK(p.dbpath == "var/lib/firebird/app.fdb");
}

TEST_CASE("firebird URI: server form with inline credentials") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri(
        "firebird://SYSDBA:masterkey@localhost/var/lib/firebird/app.fdb", u));
    CHECK_FALSE(u.embedded);
    CHECK(u.host == "localhost");
    CHECK(u.port == 0);
    CHECK(u.user == "SYSDBA");
    CHECK(u.password == "masterkey");
    CHECK(u.dbpath == "var/lib/firebird/app.fdb");
    // Default port omitted from the attach string.
    CHECK(u.attach_string() == "localhost:var/lib/firebird/app.fdb");
}

TEST_CASE("firebird URI: server form with explicit port and Windows path") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri(
        "firebird://SYSDBA:masterkey@db.host:3050/C:/data/app.fdb", u));
    CHECK(u.host == "db.host");
    CHECK(u.port == 3050);
    CHECK(u.dbpath == "C:/data/app.fdb");
    CHECK(u.attach_string() == "db.host/3050:C:/data/app.fdb");
}

TEST_CASE("firebird URI: fb:// short scheme and query options") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri(
        "fb://localhost/app.fdb?charset=UTF8&role=MANAGER", u));
    CHECK(u.host == "localhost");
    CHECK(u.dbpath == "app.fdb");
    CHECK(u.charset == "UTF8");
    CHECK(u.role == "MANAGER");
}

TEST_CASE("firebird URI: credentials via query when not inline") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri(
        "firebird:///app.fdb?user=SYSDBA&password=masterkey&charset=WIN1252", u));
    CHECK(u.embedded);
    CHECK(u.user == "SYSDBA");
    CHECK(u.password == "masterkey");
    CHECK(u.charset == "WIN1252");
}

TEST_CASE("firebird URI: inline credentials take precedence over query") {
    FirebirdUri u;
    REQUIRE(parse_firebird_uri(
        "firebird://inline:secret@localhost/app.fdb?user=other&password=nope", u));
    CHECK(u.user == "inline");
    CHECK(u.password == "secret");
}

TEST_CASE("firebird URI: rejects malformed inputs") {
    FirebirdUri u;
    CHECK_FALSE(parse_firebird_uri("odbc://Driver={x};", u));   // wrong scheme
    CHECK_FALSE(parse_firebird_uri("/local/path/app.fdb", u));  // no scheme
    CHECK_FALSE(parse_firebird_uri("firebird://", u));          // empty
    CHECK_FALSE(parse_firebird_uri("firebird:///", u));         // empty db path
    CHECK_FALSE(parse_firebird_uri("firebird://localhost", u)); // server, no db
    CHECK_FALSE(parse_firebird_uri("firebird://host:xx/db.fdb", u)); // bad port
    CHECK_FALSE(parse_firebird_uri("firebird://:3050/db.fdb", u));   // no host
}
