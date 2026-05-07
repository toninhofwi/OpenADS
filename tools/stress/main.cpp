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
    std::string   table_name = "stress.dbf";
    bool          with_indexes = false;
    bool          skip_gen = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--rows" && i + 1 < argc)
            rows = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--dir" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--table" && i + 1 < argc) table_name = argv[++i];
        else if (a == "--with-indexes")        with_indexes = true;
        else if (a == "--skip-gen")            skip_gen = true;
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
    // collide (skipped when --skip-gen leaves an existing file in
    // place to test the index path against external data).
    if (!skip_gen) {
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
    ADSHANDLE hTable = 0;
    if (!skip_gen) {
        UNSIGNED8 def[] =
          "ID,N,8,0;TAG,C,4,0;AMT,N,8,2";
        UNSIGNED8 tname[] = "stress";
        UNSIGNED32 rc = AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                                       0, 0, 0, 0, def, &hTable);
        if (rc != 0) {
            std::fprintf(stderr, "AdsCreateTable failed: %u\n", rc);
            return 1;
        }
        std::printf("[stress] writing %u rows -> %s/%s\n",
                    rows, data_dir.c_str(), table_name.c_str());
    }

    std::mt19937 rng(42);
    auto pick = [&](const char* const* arr, std::size_t n) {
        return arr[rng() % n];
    };
    double t0 = now_ms();
    UNSIGNED8 fid[]    = "ID";
    UNSIGNED8 ftag[]   = "TAG";
    UNSIGNED8 famt[]   = "AMT";
    (void)pick; (void)FIRST; (void)LAST; (void)DEPTS;

    if (skip_gen) goto run_indexes;
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

    if (hTable != 0) AdsCloseTable(hTable);
run_indexes:

    // Re-open + verify count matches before attempting indexes.
    {
        ADSHANDLE hCheck = 0;
        std::vector<UNSIGNED8> leafbuf(table_name.size() + 1);
        std::memcpy(leafbuf.data(), table_name.c_str(),
                    table_name.size() + 1);
        if (AdsOpenTable(hConn, leafbuf.data(), nullptr, ADS_CDX,
                         0, 0, 0, 0, &hCheck) == 0) {
            UNSIGNED32 rc2 = 0;
            AdsGetRecordCount(hCheck, 0, &rc2);
            std::printf("[stress] reopened %s: %u records\n",
                        table_name.c_str(), rc2);
            AdsCloseTable(hCheck);
        }
    }

    if (with_indexes) {
        // Open the table again for AdsCreateIndex61 (the direct
        // ABI entry point, bypassing the CDX-only SQL DDL path so
        // we can target either .cdx or .ntx by extension).
        ADSHANDLE hIdxTable = 0;
        std::vector<UNSIGNED8> leafbuf(table_name.size() + 1);
        std::memcpy(leafbuf.data(), table_name.c_str(),
                    table_name.size() + 1);
        if (AdsOpenTable(hConn, leafbuf.data(), nullptr, ADS_CDX,
                         0, 0, 0, 0, &hIdxTable) != 0) {
            std::fprintf(stderr, "open for index failed\n");
            return 1;
        }
        struct IdxDef { const char* file; const char* tag; const char* expr; };
        IdxDef idxs[] = {
            {"stress_id.ntx",   "ID_NTX",  "ID"},
            {"stress_tag.ntx",  "TAG_NTX", "TAG"},
        };
        for (auto& d : idxs) {
            double i0 = now_ms();
            UNSIGNED8 file_buf[64]; std::strncpy(
                reinterpret_cast<char*>(file_buf), d.file, sizeof(file_buf) - 1);
            file_buf[sizeof(file_buf) - 1] = 0;
            UNSIGNED8 tag_buf[64];  std::strncpy(
                reinterpret_cast<char*>(tag_buf), d.tag, sizeof(tag_buf) - 1);
            tag_buf[sizeof(tag_buf) - 1] = 0;
            UNSIGNED8 expr_buf[128]; std::strncpy(
                reinterpret_cast<char*>(expr_buf), d.expr, sizeof(expr_buf) - 1);
            expr_buf[sizeof(expr_buf) - 1] = 0;
            ADSHANDLE hIdx = 0;
            UNSIGNED32 ir = AdsCreateIndex61(hIdxTable, file_buf, tag_buf,
                                              expr_buf, nullptr, nullptr,
                                              0, 0, &hIdx);
            char emsg[512] = {0};
            UNSIGNED16 elen = sizeof(emsg);
            UNSIGNED32 ecode = 0;
            if (ir != 0) AdsGetLastError(&ecode,
                reinterpret_cast<UNSIGNED8*>(emsg), &elen);
            std::printf("  idx %-10s on %-18s %-12s %.1f s  rc=%u  msg=%.*s\n",
                        d.tag, d.file, d.expr,
                        (now_ms() - i0) / 1000.0, ir,
                        static_cast<int>(elen), emsg);
        }
        AdsCloseTable(hIdxTable);
    }

    AdsDisconnect(hConn);
    return 0;
}
