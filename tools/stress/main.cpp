// openads_stress — engine-side stress data generator.
//
// Builds a DBF + sidecars through the public ABI (so the layout
// is exactly what the engine itself produces and indexes / SQL
// queries see no surprises). Ships as a separate CLI so the
// concurrent-load test scripts can stage their dataset without
// pulling in Python or hand-rolled DBF writers.
//
// Usage:
//   openads_stress --rows N --dir <data dir> [--with-indexes]
//
// Output (in <data dir>):
//   stress.dbf  — N rows × 8 fields (C / N / D / L mix)
//   stress.cdx  — built when --with-indexes is passed; tags:
//                 ID_TAG, NAME_TAG (UPPER(NAME)), DEPT_TAG, DOB_TAG.

#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double, std::milli>(
               clock::now() - t0).count();
}

const char* DEPTS[] = {
    "Engineering", "Sales", "Marketing", "HR", "Finance",
    "Support", "Legal", "Ops", "Research", "Product",
    "Design", "QA"
};
const char* FIRST[] = {"Alice","Bob","Carol","Dave","Eve","Frank",
    "Grace","Henry","Iris","Julia","Kevin","Laura","Mike","Nora"};
const char* LAST[]  = {"Smith","Lopez","Martin","Brown","Garcia",
    "Davis","Wong","Liu","Patel","Rossi","Klein","Tanaka","Singh"};

} // namespace

int main(int argc, char** argv) {
    std::uint32_t rows = 100000;
    std::string   data_dir = ".";
    bool          with_indexes = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--rows" && i + 1 < argc)
            rows = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--dir" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--with-indexes")        with_indexes = true;
        else if (a == "-h" || a == "--help") {
            std::printf(
                "usage: %s [--rows N] [--dir DIR] [--with-indexes]\n",
                argv[0]);
            return 0;
        }
    }
    fs::create_directories(data_dir);

    std::vector<UNSIGNED8> srv(data_dir.size() + 1);
    std::memcpy(srv.data(), data_dir.c_str(), data_dir.size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "AdsConnect60 failed\n");
        return 1;
    }

    // Drop any pre-existing stress.dbf so AdsCreateTable doesn't
    // collide.
    {
        std::error_code ec;
        for (auto& f : {"stress.dbf", "stress.cdx", "stress.fpt",
                         "stress.dbt"}) {
            fs::remove(fs::path(data_dir) / f, ec);
        }
    }

    // Schema: 8 fields, exercises C / N / D / L; memo deferred to a
    // follow-up milestone (engine + Python generator interop bug
    // tracked separately).
    // Minimal schema first to isolate the CREATE INDEX bug —
    // bench tool's three-column DBF works fine; stress' eight-column
    // version trips 6106. Bisect by starting with bench-shape and
    // expand until the failure shows up.
    UNSIGNED8 def[] =
      "ID,N,8,0;TAG,C,4,0;AMT,N,8,2";
    UNSIGNED8 tname[] = "stress";
    ADSHANDLE hTable = 0;
    UNSIGNED32 rc = AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                                   0, 0, 0, 0, def, &hTable);
    if (rc != 0) {
        std::fprintf(stderr, "AdsCreateTable failed: %u\n", rc);
        return 1;
    }
    std::printf("[stress] writing %u rows -> %s/stress.dbf\n",
                rows, data_dir.c_str());

    std::mt19937 rng(42);
    auto pick = [&](const char* const* arr, std::size_t n) {
        return arr[rng() % n];
    };
    double t0 = now_ms();
    UNSIGNED8 fid[]    = "ID";
    UNSIGNED8 ftag[]   = "TAG";
    UNSIGNED8 famt[]   = "AMT";
    (void)pick; (void)FIRST; (void)LAST; (void)DEPTS;

    for (std::uint32_t r = 1; r <= rows; ++r) {
        AdsAppendRecord(hTable);
        AdsSetDouble(hTable, fid, static_cast<double>(r));
        char tag[5] = "AAAA";
        for (int i = 0; i < 4; ++i)
            tag[i] = static_cast<char>('A' + ((r + i) % 26));
        AdsSetString(hTable, ftag,
                     reinterpret_cast<UNSIGNED8*>(tag), 4);
        AdsSetDouble(hTable, famt,
                     static_cast<double>((rng() % 100000) / 100.0));
        if ((r % 10000) == 0) {
            std::printf("  ...%u rows (%.1f s)\n",
                        r, (now_ms() - t0) / 1000.0);
            std::fflush(stdout);
        }
    }
    AdsWriteRecord(hTable);
    std::printf("[stress] done writing in %.1f s\n",
                (now_ms() - t0) / 1000.0);

    AdsCloseTable(hTable);

    // Re-open + verify count matches before attempting indexes.
    {
        ADSHANDLE hCheck = 0;
        UNSIGNED8 leaf[] = "stress.dbf";
        if (AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hCheck) == 0) {
            UNSIGNED32 rc2 = 0;
            AdsGetRecordCount(hCheck, 0, &rc2);
            std::printf("[stress] reopened: %u records\n", rc2);
            AdsCloseTable(hCheck);
        }
    }

    if (with_indexes) {
        // Use SQL DDL — the same code path the bench tool exercises
        // and the most thoroughly tested CREATE INDEX entry point
        // in the engine.
        ADSHANDLE hStmt = 0;
        AdsCreateSQLStatement(hConn, &hStmt);
        struct IdxDef { const char* tag; const char* expr; };
        IdxDef idxs[] = {
            {"ID_TAG",   "ID"},
            {"TAG_TAG",  "TAG"},
        };
        for (auto& d : idxs) {
            double i0 = now_ms();
            char sql[256];
            std::snprintf(sql, sizeof(sql),
                          "CREATE INDEX %s ON stress.dbf (%s)",
                          d.tag, d.expr);
            ADSHANDLE hCur = 0;
            UNSIGNED32 ir = AdsExecuteSQLDirect(
                hStmt, reinterpret_cast<UNSIGNED8*>(sql), &hCur);
            if (hCur != 0) AdsCloseTable(hCur);
            std::printf("  idx %-10s %-22s %.1f s   rc=%u\n",
                        d.tag, d.expr, (now_ms() - i0) / 1000.0, ir);
        }
        AdsCloseSQLStatement(hStmt);
    }

    AdsDisconnect(hConn);
    return 0;
}
