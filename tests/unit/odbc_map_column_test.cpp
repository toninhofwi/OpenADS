#include "doctest.h"
#include "openads/ace.h"

#if defined(OPENADS_WITH_ODBC)

#include "sql_backend/odbc_backend.h"

using openads::sql_backend::map_odbc_column;

TEST_CASE("odbc map_column retains raw sql type and size") {
    // SQL_INTEGER = 4
    auto i = map_odbc_column("id", 4, false, 10, 0);
    CHECK(i.type == ADS_INTEGER);
    CHECK(i.sql_type == 4);
    CHECK(i.column_size == 10u);

    // SQL_VARCHAR = 12
    auto v = map_odbc_column("nome", 12, true, 64, 0);
    CHECK(v.type == ADS_STRING);
    CHECK(v.sql_type == 12);
    CHECK(v.column_size == 64u);
}

TEST_CASE("odbc map_column recognises date/time as ADS_DATE") {
    // SQL_TYPE_DATE = 91, SQL_TYPE_TIME = 92, SQL_TYPE_TIMESTAMP = 93
    CHECK(map_odbc_column("d", 91, true, 10, 0).type == ADS_DATE);
    CHECK(map_odbc_column("t", 92, true, 8, 0).type == ADS_DATE);
    CHECK(map_odbc_column("ts", 93, true, 23, 3).type == ADS_DATE);
    // legacy SQL_DATE = 9, SQL_TIME = 10, SQL_TIMESTAMP = 11
    CHECK(map_odbc_column("d9", 9, true, 10, 0).type == ADS_DATE);
    CHECK(map_odbc_column("ts11", 11, true, 23, 3).type == ADS_DATE);
    // raw type preserved for binding
    CHECK(map_odbc_column("ts", 93, true, 23, 3).sql_type == 93);
}

#endif
