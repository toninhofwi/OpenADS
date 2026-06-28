#pragma once

#include "engine/aggregate.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace openads::sql_backend {

struct AggregateFieldDesc {
    std::string   name;
    std::uint16_t type = 0;
};

namespace detail {

inline bool ci_field_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool field_is_numeric(const std::vector<AggregateFieldDesc>& fields,
                             const std::string& name) {
    for (const auto& f : fields) {
        if (!ci_field_equal(f.name, name)) continue;
        switch (f.type) {
            case 2: case 10: case 11: case 12:
            case 15: case 17: case 18:
                return true;
            default:
                return false;
        }
    }
    return false;
}

inline bool resolve_agg_col(const std::vector<AggregateFieldDesc>& fields,
                            const openads::engine::AggSpec& s,
                            std::string& col) {
    if (s.field.empty()) return s.fn == openads::engine::AggFn::Count;
    for (const auto& f : fields) {
        if (ci_field_equal(f.name, s.field)) {
            col = f.name;
            return true;
        }
    }
    return false;
}

}  // namespace detail

template<typename QuoteFn>
std::string build_aggregate_sql(
    const std::string& quoted_table,
    const std::string& where_sql,
    const std::vector<openads::engine::AggSpec>& specs,
    const std::vector<AggregateFieldDesc>& fields,
    QuoteFn quote_ident,
    std::string* bad_field = nullptr) {
    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (i) sql += ", ";
        const auto& s = specs[i];
        std::string col;
        if (!detail::resolve_agg_col(fields, s, col)) {
            if (bad_field) *bad_field = s.field;
            return {};
        }
        const std::string qcol = col.empty() ? col : quote_ident(col);
        switch (s.fn) {
            case openads::engine::AggFn::Count:
                sql += s.field.empty() ? "COUNT(*)" : ("COUNT(" + qcol + ")");
                break;
            case openads::engine::AggFn::Sum:
                sql += "COALESCE(SUM(" + qcol + "),0)";
                break;
            case openads::engine::AggFn::Avg:
                sql += "AVG(" + qcol + ")";
                break;
            case openads::engine::AggFn::Min:
                sql += "MIN(" + qcol + ")";
                break;
            case openads::engine::AggFn::Max:
                sql += "MAX(" + qcol + ")";
                break;
        }
        sql += " AS a" + std::to_string(i);
    }
    sql += " FROM " + quoted_table;
    if (!where_sql.empty()) sql += " WHERE (" + where_sql + ")";
    return sql;
}

inline std::vector<openads::engine::AggValue> parse_aggregate_row(
    const std::vector<openads::engine::AggSpec>& specs,
    const std::vector<AggregateFieldDesc>& fields,
    const std::vector<std::string>& vals,
    bool have_row) {
    std::vector<openads::engine::AggValue> out;
    out.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        const bool is_null = !have_row || i >= vals.size() || vals[i].empty();
        openads::engine::AggValue av;
        if (is_null) {
            av.type = openads::engine::AggType::Empty;
        } else {
            const std::string& val = vals[i];
            const bool numeric_result =
                s.fn == openads::engine::AggFn::Count ||
                s.fn == openads::engine::AggFn::Sum   ||
                s.fn == openads::engine::AggFn::Avg   ||
                detail::field_is_numeric(fields, s.field);
            if (numeric_result) {
                av.type  = openads::engine::AggType::Numeric;
                av.bytes = openads::engine::format_agg_double(
                    std::strtod(val.c_str(), nullptr));
            } else {
                av.type  = openads::engine::AggType::String;
                av.bytes = val;
            }
        }
        out.push_back(std::move(av));
    }
    return out;
}

}  // namespace openads::sql_backend