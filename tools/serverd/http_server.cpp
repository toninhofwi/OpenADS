#include "tools/serverd/http_server.h"

#if defined(OPENADS_WITH_HTTP)

#include "tools/serverd/spa_index.h"

#include "engine/data_dict.h"
#include "network/server.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

// AdsGetFieldDecimals is exported but not declared in ace.h yet —
// forward-declare locally so we can call it from the schema endpoint.
extern "C" UNSIGNED32 AdsGetFieldDecimals(ADSHANDLE hTable,
                                            UNSIGNED8* pucField,
                                            UNSIGNED16* pusDec);

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

namespace openads::studio {

namespace {

// Open a fresh ABI connection per request. Stateless web requests
// + per-request connection is the simplest correctness model:
// the engine's existing locking handles concurrent connections.
struct AbiSession {
    ADSHANDLE conn = 0;
    explicit AbiSession(const std::string& dir) {
        std::vector<UNSIGNED8> buf(dir.size() + 1);
        std::memcpy(buf.data(), dir.c_str(), dir.size() + 1);
        AdsConnect60(buf.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &conn);
    }
    ~AbiSession() {
        if (conn != 0) AdsDisconnect(conn);
    }
    bool ok() const noexcept { return conn != 0; }
};

json json_error(const std::string& msg, int http_code) {
    return json{{"error", msg}, {"http_code", http_code}};
}

// List `*.dbf` files in the data dir.
std::vector<std::string> list_dbf_files(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".dbf") out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// studio.web.0.2 — schema for a single table:
// returns column metadata (name, type letter, length, decimals)
// + record count + raw file size on disk.
json table_schema(const std::string& dir, const std::string& tname) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t n = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), n);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED16 nf = 0;
    AdsGetNumFields(hTable, &nf);
    json cols = json::array();
    for (UNSIGNED16 i = 1; i <= nf; ++i) {
        UNSIGNED8 nm[64] = {0};
        UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hTable, i, nm, &cap);
        std::string fname(reinterpret_cast<char*>(nm), cap);
        UNSIGNED16 ftype = 0;
        UNSIGNED32 flen  = 0;
        UNSIGNED16 fdec  = 0;
        std::vector<UNSIGNED8> fbuf(fname.size() + 1);
        std::memcpy(fbuf.data(), fname.data(), fname.size());
        AdsGetFieldType    (hTable, fbuf.data(), &ftype);
        AdsGetFieldLength  (hTable, fbuf.data(), &flen);
        AdsGetFieldDecimals(hTable, fbuf.data(), &fdec);
        cols.push_back(json{
            {"name",     fname},
            {"type",     ftype},
            {"length",   flen},
            {"decimals", fdec}
        });
    }
    UNSIGNED32 rc = 0;
    AdsGetRecordCount(hTable, 0, &rc);
    AdsCloseTable(hTable);
    std::error_code ec;
    auto sz = fs::file_size(fs::path(dir) / tname, ec);
    return json{
        {"table",        tname},
        {"columns",      cols},
        {"record_count", rc},
        {"file_bytes",   ec ? 0 : static_cast<std::uint64_t>(sz)}
    };
}

// studio.web.0.2 — paginated row browse for a single table.
json table_rows(const std::string& dir, const std::string& tname,
                std::uint32_t offset, std::uint32_t limit) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED16 nf = 0;
    AdsGetNumFields(hTable, &nf);
    std::vector<std::string> cnames; cnames.reserve(nf);
    for (UNSIGNED16 i = 1; i <= nf; ++i) {
        UNSIGNED8 nm[64] = {0}; UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hTable, i, nm, &cap);
        cnames.emplace_back(reinterpret_cast<char*>(nm), cap);
    }
    UNSIGNED32 rc = 0;
    AdsGetRecordCount(hTable, 0, &rc);
    json out{{"cols", cnames}, {"rows", json::array()},
             {"total", rc}, {"offset", offset}, {"limit", limit}};
    if (offset >= rc) {
        AdsCloseTable(hTable);
        return out;
    }
    AdsGotoRecord(hTable, offset + 1);
    std::uint32_t walked = 0;
    while (walked < limit) {
        UNSIGNED16 atend = 0;
        AdsAtEOF(hTable, &atend);
        if (atend) break;
        UNSIGNED32 recno = 0;
        AdsGetRecordNum(hTable, 0, &recno);
        UNSIGNED16 deleted = 0;
        AdsIsRecordDeleted(hTable, &deleted);
        json row = json::array();
        // First "column" is the recno + deleted flag for editing.
        row.push_back(json{{"_recno", recno},
                           {"_deleted", deleted != 0}});
        for (auto& cn : cnames) {
            std::vector<UNSIGNED8> fbuf(cn.size() + 1);
            std::memcpy(fbuf.data(), cn.data(), cn.size());
            UNSIGNED8 vbuf[4096] = {0};
            UNSIGNED32 vcap = sizeof(vbuf);
            if (AdsGetField(hTable, fbuf.data(), vbuf, &vcap, 0) != 0)
                vcap = 0;
            while (vcap > 0 && vbuf[vcap - 1] == ' ') --vcap;
            row.push_back(std::string(reinterpret_cast<char*>(vbuf), vcap));
        }
        out["rows"].push_back(std::move(row));
        ++walked;
        if (AdsSkip(hTable, 1) != 0) break;
    }
    AdsCloseTable(hTable);
    return out;
}

// studio.web.0.2 — append a new row by JSON {col: value, ...}.
json table_insert(const std::string& dir, const std::string& tname,
                  const json& values) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsAppendRecord(hTable) != 0) {
        AdsCloseTable(hTable);
        return json_error("AdsAppendRecord failed", 500);
    }
    if (values.is_object()) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            std::string col = it.key();
            std::string val = it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump();
            std::vector<UNSIGNED8> fbuf(col.size() + 1);
            std::memcpy(fbuf.data(), col.data(), col.size());
            std::vector<UNSIGNED8> vbuf(val.size());
            if (!val.empty()) std::memcpy(vbuf.data(), val.data(), val.size());
            AdsSetString(hTable, fbuf.data(),
                         val.empty() ? nullptr : vbuf.data(),
                         static_cast<UNSIGNED32>(val.size()));
        }
    }
    AdsWriteRecord(hTable);
    UNSIGNED32 newrec = 0;
    AdsGetRecordNum(hTable, 0, &newrec);
    AdsCloseTable(hTable);
    return json{{"recno", newrec}, {"ok", true}};
}

// studio.web.0.2 — overwrite columns in an existing record.
json table_update(const std::string& dir, const std::string& tname,
                  std::uint32_t recno, const json& values) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsGotoRecord(hTable, recno) != 0) {
        AdsCloseTable(hTable);
        return json_error("recno out of range: " + std::to_string(recno), 404);
    }
    if (values.is_object()) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            std::string col = it.key();
            std::string val = it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump();
            std::vector<UNSIGNED8> fbuf(col.size() + 1);
            std::memcpy(fbuf.data(), col.data(), col.size());
            std::vector<UNSIGNED8> vbuf(val.size());
            if (!val.empty()) std::memcpy(vbuf.data(), val.data(), val.size());
            AdsSetString(hTable, fbuf.data(),
                         val.empty() ? nullptr : vbuf.data(),
                         static_cast<UNSIGNED32>(val.size()));
        }
    }
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);
    return json{{"recno", recno}, {"ok", true}};
}

// studio.web.0.2 — mark deleted (Clipper convention) or recall.
json table_delete(const std::string& dir, const std::string& tname,
                  std::uint32_t recno, bool recall) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsGotoRecord(hTable, recno) != 0) {
        AdsCloseTable(hTable);
        return json_error("recno out of range", 404);
    }
    if (recall) AdsRecallRecord(hTable);
    else        AdsDeleteRecord(hTable);
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);
    return json{{"recno", recno}, {"deleted", !recall}};
}

// studio.web.0.3 — CREATE TABLE through SQL DDL. Body:
//   { "name": "x.dbf", "columns": [{name, type, length, decimals?}, ...] }
// Type letters follow DBF convention (C, N, L, D, M, ...).
json table_create(const std::string& dir, const json& body) {
    if (!body.is_object() || !body.contains("name") ||
        !body.contains("columns") || !body["columns"].is_array() ||
        body["columns"].empty()) {
        return json_error("body must have {name, columns[]}", 400);
    }
    std::string name = body.value("name", "");
    if (name.empty()) return json_error("missing 'name'", 400);
    std::string sql = "CREATE TABLE " + name + " (";
    bool first = true;
    for (auto& c : body["columns"]) {
        if (!first) sql += ", ";
        first = false;
        std::string cn = c.value("name", "");
        std::string ct = c.value("type", "C");
        int cl = c.value("length", 10);
        int cd = c.value("decimals", 0);
        if (cn.empty() || ct.empty()) {
            return json_error("each column needs name + type", 400);
        }
        sql += cn + " " + ct + "(" + std::to_string(cl);
        if (cd > 0) sql += "," + std::to_string(cd);
        sql += ")";
    }
    sql += ")";

    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(sess.conn, &hStmt);
    std::vector<UNSIGNED8> sqlbuf(sql.size() + 1);
    std::memcpy(sqlbuf.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rrc = AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur);
    if (hCur != 0) AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    if (rrc != 0) return json_error("CREATE TABLE failed: " + sql, 400);
    return json{{"ok", true}, {"sql", sql}};
}

// studio.web.0.3 — DROP TABLE. Removes the .dbf + matching
// sidecars (.cdx / .ntx / .dbt / .fpt / .dbv / .lck) on disk.
json table_drop(const std::string& dir, const std::string& tname) {
    fs::path base = fs::path(dir) / tname;
    std::error_code ec;
    if (!fs::exists(base, ec)) {
        return json_error("table not found: " + tname, 404);
    }
    std::vector<std::string> sidecar_exts =
        {".cdx", ".ntx", ".dbt", ".fpt", ".dbv", ".lck"};
    int removed = 0;
    fs::remove(base, ec); ++removed;
    fs::path stem = base.parent_path() / base.stem();
    for (auto& ext : sidecar_exts) {
        fs::path p = stem; p += ext;
        if (fs::exists(p, ec)) {
            fs::remove(p, ec);
            ++removed;
        }
    }
    return json{{"ok", true}, {"removed", removed}, {"table", tname}};
}

// studio.web.0.3 — encrypt a DBF in place via AdsEncryptTable.
// Body: { "password": "..." }. Sets the connection encryption
// password first (M11.2), then encrypts the table.
json table_encrypt(const std::string& dir, const std::string& tname,
                   const json& body) {
    std::string pw = body.value("password", "");
    if (pw.empty()) return json_error("missing 'password'", 400);
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    std::vector<UNSIGNED8> pwbuf(pw.size() + 1);
    std::memcpy(pwbuf.data(), pw.c_str(), pw.size() + 1);
    AdsSetEncryptionPassword(sess.conn, pwbuf.data());
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr, ADS_CDX,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED32 rc = AdsEncryptTable(hTable);
    AdsCloseTable(hTable);
    if (rc != 0) return json_error(
        "AdsEncryptTable failed (" + std::to_string(rc) + ")", 500);
    return json{{"ok", true}, {"table", tname}};
}

// studio.web.0.5 — Data Dictionary surface.
//
// Each request opens a fresh DataDict, mutates if needed, calls
// save(), drops it. Stateless web => safe under concurrent edits;
// the DD's own write-then-rename keeps the on-disk file
// crash-consistent.

std::vector<std::string> list_add_files(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".add") out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

json dd_list(const std::string& dir) {
    auto names = list_add_files(dir);
    json arr = json::array();
    for (auto& n : names) {
        std::error_code ec;
        auto sz = fs::file_size(fs::path(dir) / n, ec);
        arr.push_back(json{
            {"name",  n},
            {"bytes", ec ? 0 : static_cast<std::uint64_t>(sz)}
        });
    }
    return json{{"dicts", arr}, {"data_dir", dir}};
}

json dd_view(const std::string& dir, const std::string& name) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) {
        return json_error("DataDict::open failed: " + p, 404);
    }
    auto& dd = dd_r.value();

    json tables = json::array();
    // DataDict exposes tables_ via has_alias / resolve only; use
    // a private accessor via DBPROP iteration is not available.
    // Workaround: re-parse the file ourselves to enumerate every
    // row. (DataDict doesn't expose a list_tables() API yet.)
    {
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            // crude split on first run of spaces
            std::size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos) continue;
            std::string kind = line.substr(0, sp);
            std::string rest = line.substr(sp);
            // ltrim
            std::size_t a = rest.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            rest = rest.substr(a);
            if (kind == "TABLE") {
                auto eq = rest.find('=');
                if (eq != std::string::npos) {
                    tables.push_back(json{
                        {"alias", rest.substr(0, eq)},
                        {"path",  rest.substr(eq + 1)}
                    });
                }
            }
        }
    }

    json indexes = json::array();
    for (auto& ix : dd.indexes()) {
        indexes.push_back(json{
            {"table",   ix.table_alias},
            {"path",    ix.index_path},
            {"comment", ix.comment}
        });
    }

    json links = json::array();
    for (auto& kv : dd.links()) {
        links.push_back(json{
            {"alias", kv.second.alias},
            {"path",  kv.second.path},
            {"user",  kv.second.user}
        });
    }

    json ri = json::array();
    for (auto& kv : dd.ri()) {
        ri.push_back(json{
            {"name",        kv.second.name},
            {"parent",      kv.second.parent},
            {"child",       kv.second.child},
            {"tag",         kv.second.tag},
            {"update_opt",  kv.second.update_opt},
            {"delete_opt",  kv.second.delete_opt},
            {"fail_table",  kv.second.fail_table}
        });
    }

    // Re-parse for users + props (DataDict doesn't expose iteration
    // of users_ or db_props_ directly today).
    json users = json::array();
    json props = json::array();
    {
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos) continue;
            std::string kind = line.substr(0, sp);
            std::string rest = line.substr(sp);
            std::size_t a = rest.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            rest = rest.substr(a);
            if (kind == "USER") users.push_back(rest);
            else if (kind == "DBPROP") {
                auto eq = rest.find('=');
                if (eq != std::string::npos) {
                    props.push_back(json{
                        {"key",   rest.substr(0, eq)},
                        {"value", rest.substr(eq + 1)}
                    });
                }
            }
        }
    }

    return json{
        {"name",    name},
        {"path",    p},
        {"tables",  tables},
        {"indexes", indexes},
        {"users",   users},
        {"links",   links},
        {"ri",      ri},
        {"db_props", props}
    };
}

json dd_create(const std::string& dir, const std::string& name) {
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) {
        return json_error("dictionary already exists: " + name, 409);
    }
    auto r = openads::engine::DataDict::create(p.string());
    if (!r) return json_error("DataDict::create failed", 500);
    return json{{"ok", true}, {"name", name}};
}

json dd_drop(const std::string& dir, const std::string& name) {
    fs::path p = fs::path(dir) / name;
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return json_error("dictionary not found: " + name, 404);
    }
    fs::remove(p, ec);
    return json{{"ok", true}, {"removed", name}};
}

json dd_add_table(const std::string& dir, const std::string& name,
                  const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string alias = body.value("alias", "");
    std::string rel   = body.value("path",  "");
    if (alias.empty() || rel.empty()) {
        return json_error("body needs {alias, path}", 400);
    }
    if (auto r = dd.add_table(alias, rel); !r) {
        return json_error("add_table failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"alias", alias}, {"path", rel}};
}

json dd_remove_table(const std::string& dir, const std::string& name,
                     const std::string& alias) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    if (auto r = dd.remove_table(alias); !r) {
        return json_error("remove_table failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"alias", alias}};
}

json dd_add_user(const std::string& dir, const std::string& name,
                 const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string user = body.value("user", "");
    if (user.empty()) return json_error("body needs {user}", 400);
    if (auto r = dd.create_user(user); !r) {
        return json_error("create_user failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"user", user}};
}

json dd_remove_user(const std::string& dir, const std::string& name,
                    const std::string& user) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    if (auto r = dd.delete_user(user); !r) {
        return json_error("delete_user failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"user", user}};
}

json dd_set_dbprop(const std::string& dir, const std::string& name,
                   const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string k = body.value("key",   "");
    std::string v = body.value("value", "");
    if (k.empty()) return json_error("body needs {key, value}", 400);
    if (auto r = dd.set_db_property(k, v); !r) {
        return json_error("set_db_property failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"key", k}};
}

// studio.web.0.2 — server info panel.
json server_info(const std::string& dir) {
    json out{
        {"engine",      "openads"},
        {"version",     "1.0.0-rc1"},
        {"data_dir",    dir},
        {"http",        "studio.web.0.2"}
    };
    AbiSession sess(dir);
    if (sess.ok()) {
        UNSIGNED8 buf[256] = {0};
        UNSIGNED16 cap = sizeof(buf);
        if (AdsGetServerName(sess.conn, buf, &cap) == 0) {
            out["server_name"] = std::string(reinterpret_cast<char*>(buf), cap);
        }
    }
    out["tables"] = list_dbf_files(dir);
    return out;
}

// Run a SQL string + materialise up to `limit` rows as a row-major
// table { cols, rows }.
json run_sql(const std::string& dir, const std::string& sql,
             std::uint32_t limit) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir: " + dir, 500);

    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(sess.conn, &hStmt);
    std::vector<UNSIGNED8> sqlbuf(sql.size() + 1);
    std::memcpy(sqlbuf.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rrc = AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur);
    if (rrc != 0) {
        // Capture the engine error BEFORE AdsCloseSQLStatement —
        // ok() inside the close clears thread-local last_error.
        char emsg[512] = {0};
        UNSIGNED16 elen = sizeof(emsg);
        UNSIGNED32 ecode = 0;
        AdsGetLastError(&ecode,
                        reinterpret_cast<UNSIGNED8*>(emsg), &elen);
        AdsCloseSQLStatement(hStmt);
        std::string detail(emsg, elen);
        // Soft heuristics — surface the most common SQL-101 traps
        // back to the user as a "did you mean…?" hint.
        std::string hint;
        if (sql.find('"') != std::string::npos) {
            hint = " hint: ADS SQL uses single quotes for string "
                   "literals — try '...' instead of \"...\".";
        }
        return json_error(
            "AdsExecuteSQLDirect failed (" +
            std::to_string(rrc) + ")" +
            (detail.empty() ? "" : ": " + detail) + hint,
            400);
    }
    json out{{"cols", json::array()}, {"rows", json::array()},
             {"rows_returned", 0}};
    if (hCur == 0) {
        AdsCloseSQLStatement(hStmt);
        return out;
    }
    UNSIGNED16 nfields = 0;
    AdsGetNumFields(hCur, &nfields);
    std::vector<std::string> col_names;
    col_names.reserve(nfields);
    for (UNSIGNED16 i = 1; i <= nfields; ++i) {
        UNSIGNED8 nm[128] = {0};
        UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hCur, i, nm, &cap);
        col_names.emplace_back(reinterpret_cast<char*>(nm), cap);
        out["cols"].push_back(col_names.back());
    }
    AdsGotoTop(hCur);
    UNSIGNED16 atend = 0;
    AdsAtEOF(hCur, &atend);
    std::uint32_t walked = 0;
    while (atend == 0 && walked < limit) {
        json row = json::array();
        for (auto& cn : col_names) {
            UNSIGNED8 fbuf[64] = {0};
            std::size_t n = std::min<std::size_t>(cn.size(), sizeof(fbuf) - 1);
            std::memcpy(fbuf, cn.data(), n);
            UNSIGNED8 vbuf[4096] = {0};
            UNSIGNED32 vcap = sizeof(vbuf);
            UNSIGNED32 fr = AdsGetField(hCur, fbuf, vbuf, &vcap, 0);
            if (fr != 0) vcap = 0;
            // Trim trailing spaces (DBF blank-pads fixed-width).
            while (vcap > 0 && vbuf[vcap - 1] == ' ') --vcap;
            row.push_back(std::string(reinterpret_cast<char*>(vbuf), vcap));
        }
        out["rows"].push_back(std::move(row));
        ++walked;
        AdsSkip(hCur, 1);
        AdsAtEOF(hCur, &atend);
    }
    out["rows_returned"] = walked;
    AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    return out;
}

} // namespace

HttpConsole::HttpConsole() : srv_(std::make_unique<httplib::Server>()) {}
HttpConsole::~HttpConsole() { stop(); }

bool HttpConsole::start(const std::string& host,
                         std::uint16_t      port,
                         const std::string& data_dir,
                         openads::network::Server* wire_srv) {
    data_dir_ = data_dir;
    wire_srv_ = wire_srv;
    auto& srv = *srv_;

    srv.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kSpaIndexHtml, "text/html; charset=utf-8");
    });

    srv.Get("/api/health", [this](const httplib::Request&,
                                   httplib::Response& res) {
        json j{{"status", "ok"},
               {"engine", "openads"},
               {"data_dir", data_dir_}};
        res.set_content(j.dump(), "application/json");
    });

    srv.Get("/api/tables",
            [this](const httplib::Request&, httplib::Response& res) {
        auto tables = list_dbf_files(data_dir_);
        json j{{"data_dir", data_dir_},
               {"tables",   tables}};
        res.set_content(j.dump(), "application/json");
    });

    srv.Get(R"(/api/tables/([^/]+)/schema)",
            [this](const httplib::Request& req, httplib::Response& res) {
        json j = table_schema(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get(R"(/api/tables/([^/]+)/rows)",
            [this](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t offset = 0, limit = 50;
        if (req.has_param("offset"))
            offset = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("offset").c_str(), nullptr, 10));
        if (req.has_param("limit"))
            limit  = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("limit").c_str(), nullptr, 10));
        if (limit == 0 || limit > 5000) limit = 50;
        json j = table_rows(data_dir_, req.matches[1], offset, limit);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/([^/]+)/insert)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_insert(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/([^/]+)/update)",
             [this](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t recno = 0;
        if (req.has_param("recno"))
            recno = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("recno").c_str(), nullptr, 10));
        if (recno == 0) { res.status = 400;
            res.set_content(json_error("missing recno query param", 400).dump(),
                            "application/json"); return; }
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_update(data_dir_, req.matches[1], recno, body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/([^/]+)/delete)",
             [this](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t recno = 0;
        if (req.has_param("recno"))
            recno = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("recno").c_str(), nullptr, 10));
        bool recall = req.has_param("recall") &&
                      req.get_param_value("recall") == "1";
        if (recno == 0) { res.status = 400;
            res.set_content(json_error("missing recno query param", 400).dump(),
                            "application/json"); return; }
        json j = table_delete(data_dir_, req.matches[1], recno, recall);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get("/api/server/info",
            [this](const httplib::Request&, httplib::Response& res) {
        json j = server_info(data_dir_);
        res.set_content(j.dump(), "application/json");
    });

    // studio.web.0.5 — Data Dictionary endpoints.
    srv.Get("/api/dd",
            [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(dd_list(data_dir_).dump(), "application/json");
    });
    srv.Post("/api/dd",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        std::string n = body.value("name", "");
        if (n.empty()) { res.status = 400;
            res.set_content(json_error("body needs {name}", 400).dump(),
                            "application/json"); return; }
        json j = dd_create(data_dir_, n);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Get(R"(/api/dd/([^/]+))",
            [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_view(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_drop(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/tables)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_add_table(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+)/tables/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_remove_table(data_dir_, req.matches[1], req.matches[2]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/users)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_add_user(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+)/users/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_remove_user(data_dir_, req.matches[1], req.matches[2]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/dbprop)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_set_dbprop(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get("/api/server/sessions",
            [this](const httplib::Request&, httplib::Response& res) {
        if (wire_srv_ == nullptr) {
            res.set_content(json{{"sessions", json::array()},
                                 {"hint", "wire server unavailable"}}
                            .dump(), "application/json");
            return;
        }
        auto now = std::chrono::system_clock::now();
        json arr = json::array();
        for (auto& s : wire_srv_->sessions_snapshot()) {
            auto secs = [&](auto t){
                return std::chrono::duration_cast<std::chrono::seconds>(
                    now - t).count();
            };
            arr.push_back(json{
                {"id",            s.id},
                {"peer_ip",       s.peer_ip},
                {"peer_port",     s.peer_port},
                {"user",          s.user},
                {"data_dir",      s.data_dir},
                {"connected_secs", static_cast<long long>(secs(s.connected_at))},
                {"idle_secs",      static_cast<long long>(secs(s.last_activity))},
                {"frames_in",     s.frames_in},
                {"frames_out",    s.frames_out},
                {"open_tables",   s.open_tables}
            });
        }
        res.set_content(json{{"sessions", arr},
                             {"count", arr.size()}}.dump(),
                        "application/json");
    });

    srv.Post("/api/tables",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_create(data_dir_, body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Delete(R"(/api/tables/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = table_drop(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/([^/]+)/encrypt)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_encrypt(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post("/api/sql",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(json_error("invalid JSON body", 400).dump(),
                            "application/json");
            return;
        }
        std::string sql = body.value("sql", "");
        std::uint32_t limit = body.value("limit", 200u);
        if (sql.empty()) {
            res.status = 400;
            res.set_content(json_error("missing 'sql' field", 400).dump(),
                            "application/json");
            return;
        }
        json j = run_sql(data_dir_, sql, limit);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    if (!srv.bind_to_port(host, port)) return false;
    running_.store(true);
    thread_ = std::thread([this]() {
        srv_->listen_after_bind();
        running_.store(false);
    });
    return true;
}

void HttpConsole::stop() {
    if (!running_.load() && !thread_.joinable()) return;
    if (srv_) srv_->stop();
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

} // namespace openads::studio

#endif // OPENADS_WITH_HTTP
