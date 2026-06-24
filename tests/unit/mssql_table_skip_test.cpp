#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/mssql_table.h"
#include "sql_backend/tds_protocol.h"

#include <string>

using openads::sql_backend::MssqlTable;
using namespace openads::sql_backend::tds;

namespace {
// Build a buffered result of `n` single-column rows whose value is the
// stringified 0-based row index, so a read confirms WHICH row we land on.
QueryResult make_rows(int n) {
    QueryResult qr;
    qr.ok = true;
    TdsColumn col;
    col.name       = "id";
    col.type_token = 0x26;  // INTN
    col.length     = 4;
    qr.columns.push_back(col);
    for (int i = 0; i < n; ++i) {
        TdsCell cell;
        cell.value   = std::to_string(i);
        cell.is_null = false;
        qr.rows.push_back({cell});
    }
    return qr;
}
}  // namespace

// A backward SKIP that lands EXACTLY on the first row must read row 0, not
// report begin-of-file. Regression for the off-by-one `abs_n >= pos`
// (it must be `>`): skipping back `pos` rows reaches index 0, a valid row.
TEST_CASE("MssqlTable::skip backward onto the first row reads row 0 (not BOF)") {
    auto t = MssqlTable::from_result(make_rows(5));

    t->go_top();   // pos = 0, on the first row
    t->skip(3);    // pos = 3, the row whose value is "3"
    CHECK(t->at_bof() == false);
    CHECK(t->record_num() == 4u);

    t->skip(-3);   // back to pos = 0 — the first row, a valid position
    CHECK(t->at_bof() == false);
    CHECK(t->at_eof() == false);
    CHECK(t->record_num() == 1u);

    std::string v;
    bool        is_null = true;
    REQUIRE(t->get_field(0, v, is_null) == true);
    CHECK(is_null == false);
    CHECK(v == "0");
}

// Skipping back PAST the first row still parks at BOF (unchanged behaviour).
TEST_CASE("MssqlTable::skip backward past the first row parks at BOF") {
    auto t = MssqlTable::from_result(make_rows(5));

    t->go_top();
    t->skip(2);    // pos = 2
    t->skip(-3);   // 3 > 2 -> stepped past the beginning
    CHECK(t->at_bof() == true);
    CHECK(t->record_num() == 0u);

    std::string v;
    bool        is_null = false;
    CHECK(t->get_field(0, v, is_null) == false);
}

#endif  // defined(OPENADS_WITH_MSSQL)
