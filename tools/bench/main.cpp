// openads_bench — synthetic SQL workload timer for cross-platform
// comparison. Generates a temp DBF on the fly, runs a fixed set of
// SQL queries through the public ABI (so the numbers reflect the
// same path a Harbour / rddads app would hit), prints CSV.
//
// usage: openads_bench [--rows N] [--repeats R] [--csv]
//   --rows    rows in the synthetic DBF (default 100000)
//   --repeats per-workload repeats (default 5; reports median)
//   --csv     emit CSV header + one row per workload
//
// Output columns: workload,rows,run_ms_min,run_ms_med,run_ms_max,note

#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// Build a synthetic DBF with three columns:
//   ID    N(8,0)   sequential 1..N
//   TAG   C(4)     'AAAA' .. cyclic 4-char tags
//   AMT   N(8,2)   random 0..1000
fs::path stage_bench_dbf(const fs::path& dir, std::uint32_t rows) {
    fs::create_directories(dir);
    fs::path p = dir / "bench.dbf";
    fs::remove(p);

    constexpr std::uint16_t hdr_len = 32 + 32 * 3 + 1;
    constexpr std::uint16_t rec_len = 1 + 8 + 4 + 8;          // 21
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>( rows         & 0xFFu);
    hdr[5] = static_cast<std::uint8_t>((rows >>  8 ) & 0xFFu);
    hdr[6] = static_cast<std::uint8_t>((rows >> 16 ) & 0xFFu);
    hdr[7] = static_cast<std::uint8_t>((rows >> 24 ) & 0xFFu);
    hdr[8]  = static_cast<std::uint8_t>( hdr_len       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hdr_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rec_len       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());
    auto fld = [&](const char* n, char t, std::uint8_t L, std::uint8_t d) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), n, 11);
        fd[11] = static_cast<std::uint8_t>(t);
        fd[16] = L; fd[17] = d;
        file.insert(file.end(), fd.begin(), fd.end());
    };
    fld("ID",  'N', 8, 0);
    fld("TAG", 'C', 4, 0);
    fld("AMT", 'N', 8, 2);
    file.push_back(0x0D);

    file.reserve(file.size() + rec_len * static_cast<std::size_t>(rows) + 1);
    char buf[32];
    std::uint32_t seed = 0x12345678u;
    for (std::uint32_t r = 1; r <= rows; ++r) {
        file.push_back(' ');
        std::snprintf(buf, sizeof(buf), "%8u", r);
        for (int i = 0; i < 8; ++i) file.push_back(static_cast<std::uint8_t>(buf[i]));
        std::array<std::uint8_t, 4> tag{};
        for (int i = 0; i < 4; ++i) {
            tag[i] = static_cast<std::uint8_t>('A' + ((r + static_cast<std::uint32_t>(i)) % 26));
        }
        for (int i = 0; i < 4; ++i)
            file.push_back(tag[i]);
        seed = seed * 1664525u + 1013904223u;
        double amt = static_cast<double>((seed >> 8) % 100000u);
        amt /= 100.0;
        std::snprintf(buf, sizeof(buf), "%8.2f", amt);
        for (int i = 0; i < 8; ++i) file.push_back(static_cast<std::uint8_t>(buf[i]));
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

double run_query(ADSHANDLE hStmt, const std::string& sql,
                 std::uint32_t* out_rows = nullptr) {
    std::vector<UNSIGNED8> b(sql.size() + 1);
    std::memcpy(b.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    double t = now_ms();
    if (AdsExecuteSQLDirect(hStmt, b.data(), &hCur) != 0) return -1.0;
    UNSIGNED32 cnt = 0;
    if (hCur != 0) {
        AdsGetRecordCount(hCur, 0, &cnt);
        // walk rows so the time reflects materialisation cost
        AdsGotoTop(hCur);
        for (std::uint32_t i = 0; i < cnt && i < 10; ++i) {
            UNSIGNED8 fld[16] = "ID";
            UNSIGNED8 buf[32]{};
            UNSIGNED32 cap = sizeof(buf);
            (void)AdsGetField(hCur, fld, buf, &cap, 0);
            (void)AdsSkip(hCur, 1);
        }
        AdsCloseTable(hCur);
    }
    double dt = now_ms() - t;
    if (out_rows) *out_rows = cnt;
    return dt;
}

struct WorkloadResult {
    std::string  name;
    std::uint32_t rows = 0;
    double       min_ms = 0, med_ms = 0, max_ms = 0;
    std::string  note;
};

WorkloadResult time_workload(const std::string& name,
                             ADSHANDLE hStmt,
                             const std::string& sql,
                             int repeats,
                             const std::string& note = "") {
    WorkloadResult r;
    r.name = name;
    r.note = note;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));
    std::uint32_t rows = 0;
    for (int i = 0; i < repeats; ++i) {
        double dt = run_query(hStmt, sql, &rows);
        if (dt >= 0) samples.push_back(dt);
    }
    if (samples.empty()) {
        r.note = note + (note.empty() ? "" : " | ") + "FAILED";
        return r;
    }
    std::sort(samples.begin(), samples.end());
    r.rows   = rows;
    r.min_ms = samples.front();
    r.med_ms = samples[samples.size() / 2];
    r.max_ms = samples.back();
    return r;
}

} // namespace

int main(int argc, char** argv) {
    std::uint32_t rows = 100000;
    int           repeats = 5;
    bool          csv = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--rows"    && i + 1 < argc) rows    = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--repeats" && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (a == "--csv")                     csv     = true;
        else if (a == "-h" || a == "--help") {
            std::printf("usage: %s [--rows N] [--repeats R] [--csv]\n", argv[0]);
            return 0;
        }
    }

    auto dir = fs::temp_directory_path() / "openads_bench";
    std::error_code ec;
    fs::remove_all(dir, ec);
    double t_stage = now_ms();
    auto p = stage_bench_dbf(dir, rows);
    double stage_ms = now_ms() - t_stage;

    std::vector<UNSIGNED8> srv(dir.string().size() + 1);
    std::memcpy(srv.data(), dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        std::fprintf(stderr, "AdsConnect60 failed\n");
        return 1;
    }
    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(hConn, &hStmt);

    std::vector<WorkloadResult> results;
    results.push_back({"stage_dbf", rows, stage_ms, stage_ms, stage_ms,
                       "build synthetic DBF"});

    results.push_back(time_workload("seq_walk_count",   hStmt,
        "SELECT COUNT(*) FROM bench.dbf", repeats,
        "full-table count"));

    results.push_back(time_workload("seq_walk_where",   hStmt,
        "SELECT COUNT(*) FROM bench.dbf WHERE TAG = 'AAAA'", repeats,
        "WHERE filter"));

    results.push_back(time_workload("agg_sum_avg",      hStmt,
        "SELECT SUM(AMT), AVG(AMT), MIN(AMT), MAX(AMT) FROM bench.dbf",
        repeats, "aggregate over all rows"));

    results.push_back(time_workload("group_by",         hStmt,
        "SELECT COUNT(*) FROM bench.dbf GROUP BY TAG",
        repeats, "26-group GROUP BY"));

    results.push_back(time_workload("order_by_top10",   hStmt,
        "SELECT * FROM bench.dbf ORDER BY AMT DESC LIMIT 10",
        repeats, "top-10 by AMT"));

    results.push_back(time_workload("distinct_tags",    hStmt,
        "SELECT DISTINCT TAG FROM bench.dbf",
        repeats, "26 distinct tags"));

    results.push_back(time_workload("between_filter",   hStmt,
        "SELECT COUNT(*) FROM bench.dbf WHERE AMT BETWEEN 100 AND 500",
        repeats, "BETWEEN range"));

    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);

    if (csv) {
        std::printf("workload,rows,run_ms_min,run_ms_med,run_ms_max,note\n");
        for (auto& r : results) {
            std::printf("%s,%u,%.3f,%.3f,%.3f,%s\n",
                        r.name.c_str(), r.rows,
                        r.min_ms, r.med_ms, r.max_ms,
                        r.note.c_str());
        }
    } else {
        std::printf("OpenADS bench (rows=%u, repeats=%d)\n", rows, repeats);
        std::printf("%-22s %10s %10s %10s  %s\n",
                    "workload", "min(ms)", "med(ms)", "max(ms)", "note");
        for (auto& r : results) {
            std::printf("%-22s %10.2f %10.2f %10.2f  %s\n",
                        r.name.c_str(), r.min_ms, r.med_ms, r.max_ms,
                        r.note.c_str());
        }
    }
    return 0;
}
