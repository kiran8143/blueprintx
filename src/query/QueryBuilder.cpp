// Author: Udaykiran Atta
// License: MIT

#include "query/QueryBuilder.h"
#include "schema/ModelRegistry.h"
#include "config/EnvConfig.h"

#include <sstream>
#include <stdexcept>
#include <algorithm>

QueryBuilder::QueryBuilder(const drogon::orm::DbClientPtr& db)
    : db_(db) {}

QueryBuilder& QueryBuilder::table(const std::string& tableName) {
    auto& registry = ModelRegistry::instance();
    tableMeta_ = registry.getTable(tableName);
    if (!tableMeta_) {
        throw std::invalid_argument(
            "Table '" + tableName + "' not found in ModelRegistry");
    }
    tableName_ = tableName;
    return *this;
}

QueryBuilder& QueryBuilder::select(const std::vector<std::string>& columns) {
    for (const auto& col : columns) {
        validateColumn(col);
    }
    selectColumns_ = columns;
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& column,
                                    const std::string& op,
                                    const Json::Value& value) {
    validateColumn(column);

    static const std::vector<std::string> validOps = {
        "=", "!=", "<>", "<", ">", "<=", ">=", "LIKE", "NOT LIKE"
    };
    std::string upperOp = op;
    std::transform(upperOp.begin(), upperOp.end(), upperOp.begin(), ::toupper);

    bool found = false;
    for (const auto& valid : validOps) {
        if (upperOp == valid) { found = true; break; }
    }
    if (!found) {
        throw std::invalid_argument("Invalid operator: " + op);
    }

    WhereCondition cond;
    cond.column = column;
    cond.op = upperOp;
    cond.values.push_back(jsonValueToString(value));
    whereConditions_.push_back(std::move(cond));
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& column,
                                    const Json::Value& value) {
    return where(column, "=", value);
}

QueryBuilder& QueryBuilder::whereNull(const std::string& column) {
    validateColumn(column);
    WhereCondition cond;
    cond.column = column;
    cond.op = "IS NULL";
    cond.isNullCheck = true;
    whereConditions_.push_back(std::move(cond));
    return *this;
}

QueryBuilder& QueryBuilder::whereNotNull(const std::string& column) {
    validateColumn(column);
    WhereCondition cond;
    cond.column = column;
    cond.op = "IS NOT NULL";
    cond.isNullCheck = true;
    whereConditions_.push_back(std::move(cond));
    return *this;
}

QueryBuilder& QueryBuilder::whereIn(const std::string& column,
                                      const Json::Value& values) {
    validateColumn(column);
    if (!values.isArray() || values.empty()) {
        throw std::invalid_argument("whereIn requires a non-empty JSON array");
    }

    WhereCondition cond;
    cond.column = column;
    cond.op = "IN";
    for (const auto& v : values) {
        cond.values.push_back(jsonValueToString(v));
    }
    whereConditions_.push_back(std::move(cond));
    return *this;
}

QueryBuilder& QueryBuilder::orderBy(const std::string& column,
                                      const std::string& direction) {
    validateColumn(column);
    std::string dir = direction;
    std::transform(dir.begin(), dir.end(), dir.begin(), ::toupper);
    if (dir != "ASC" && dir != "DESC") {
        throw std::invalid_argument("Order direction must be ASC or DESC");
    }
    orderByClauses_.push_back({column, dir});
    return *this;
}

QueryBuilder& QueryBuilder::limit(size_t count) {
    limit_ = count;
    return *this;
}

QueryBuilder& QueryBuilder::offset(size_t count) {
    offset_ = count;
    return *this;
}

QueryBuilder& QueryBuilder::reset() {
    tableName_.clear();
    tableMeta_ = nullptr;
    selectColumns_.clear();
    whereConditions_.clear();
    orderByClauses_.clear();
    limit_.reset();
    offset_.reset();
    return *this;
}

// --- SQL Generation ---

drogon::Task<drogon::orm::Result> QueryBuilder::executeSelect() {
    if (tableName_.empty()) {
        throw std::logic_error("No table specified for SELECT");
    }

    std::vector<std::string> params;
    std::ostringstream sql;

    sql << "SELECT ";
    if (selectColumns_.empty()) {
        sql << "*";
    } else {
        for (size_t i = 0; i < selectColumns_.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << quoteIdentifier(selectColumns_[i]);
        }
    }
    sql << " FROM " << quoteIdentifier(tableName_);
    sql << buildWhereClause(params);
    sql << buildOrderByClause();
    sql << buildLimitOffsetClause();

    co_return co_await execDynamic(sql.str(), std::move(params));
}

drogon::Task<drogon::orm::Result> QueryBuilder::executeInsert(
    const Json::Value& data) {
    if (tableName_.empty()) {
        throw std::logic_error("No table specified for INSERT");
    }
    if (!data.isObject() || data.empty()) {
        throw std::invalid_argument("INSERT data must be a non-empty JSON object");
    }

    std::vector<std::string> params;
    std::ostringstream sql;
    std::ostringstream valuePlaceholders;

    sql << "INSERT INTO " << quoteIdentifier(tableName_) << " (";

    auto members = data.getMemberNames();
    int placeholderIdx = 1;
    for (size_t i = 0; i < members.size(); ++i) {
        validateColumn(members[i]);
        if (i > 0) {
            sql << ", ";
            valuePlaceholders << ", ";
        }
        sql << quoteIdentifier(members[i]);
        valuePlaceholders << placeholder(placeholderIdx);
        params.push_back(jsonValueToString(data[members[i]]));
    }

    sql << ") VALUES (" << valuePlaceholders.str() << ")";
    if (!isMysql()) {
        sql << " RETURNING *";
    }

    auto result = co_await execDynamic(sql.str(), std::move(params));

    // MySQL: RETURNING not supported, query the inserted row
    if (isMysql() && tableMeta_ && !tableMeta_->primaryKeys.empty()) {
        auto lastId = co_await execDynamic("SELECT LAST_INSERT_ID() AS id", {});
        if (lastId.size() > 0) {
            auto idStr = lastId[0]["id"].as<std::string>();
            const auto& pk = tableMeta_->primaryKeys[0];
            int idx = 1;
            auto selectSql = "SELECT * FROM " + quoteIdentifier(tableName_) +
                " WHERE " + quoteIdentifier(pk) + " = " + placeholder(idx);
            std::vector<std::string> selectParams = {idStr};
            result = co_await execDynamic(selectSql, std::move(selectParams));
        }
    }
    co_return result;
}

drogon::Task<drogon::orm::Result> QueryBuilder::executeUpdate(
    const Json::Value& data) {
    if (tableName_.empty()) {
        throw std::logic_error("No table specified for UPDATE");
    }
    if (!data.isObject() || data.empty()) {
        throw std::invalid_argument("UPDATE data must be a non-empty JSON object");
    }

    std::vector<std::string> params;
    std::ostringstream sql;

    sql << "UPDATE " << quoteIdentifier(tableName_) << " SET ";

    auto members = data.getMemberNames();
    int placeholderIdx = 1;
    for (size_t i = 0; i < members.size(); ++i) {
        validateColumn(members[i]);
        if (i > 0) sql << ", ";

        if (data[members[i]].isNull()) {
            sql << quoteIdentifier(members[i]) << " = NULL";
        } else {
            sql << quoteIdentifier(members[i]) << " = " << placeholder(placeholderIdx);
            params.push_back(jsonValueToString(data[members[i]]));
        }
    }

    if (isMysql()) {
        // MySQL: ? placeholders, no renumbering needed
        std::vector<std::string> whereParams;
        std::string whereClause = buildWhereClause(whereParams);
        sql << whereClause;
        params.insert(params.end(), whereParams.begin(), whereParams.end());
    } else {
        // PostgreSQL/SQLite: $N placeholders, need renumbering
        std::vector<std::string> whereParams;
        std::string whereClause = buildWhereClause(whereParams);
        if (!whereClause.empty()) {
            std::string renumbered;
            for (size_t i = 0; i < whereClause.size(); ++i) {
                if (whereClause[i] == '$' && i + 1 < whereClause.size() &&
                    std::isdigit(whereClause[i + 1])) {
                    size_t numEnd = i + 1;
                    while (numEnd < whereClause.size() &&
                           std::isdigit(whereClause[numEnd])) {
                        ++numEnd;
                    }
                    renumbered += "$" + std::to_string(placeholderIdx++);
                    i = numEnd - 1;
                } else {
                    renumbered += whereClause[i];
                }
            }
            sql << renumbered;
            params.insert(params.end(), whereParams.begin(), whereParams.end());
        }
        sql << " RETURNING *";
    }

    auto result = co_await execDynamic(sql.str(), std::move(params));

    // MySQL: query updated row since RETURNING not supported
    if (isMysql() && result.affectedRows() > 0 &&
        tableMeta_ && !tableMeta_->primaryKeys.empty() &&
        !whereConditions_.empty()) {
        const auto& pk = tableMeta_->primaryKeys[0];
        for (const auto& cond : whereConditions_) {
            if (cond.column == pk && !cond.values.empty()) {
                int idx = 1;
                auto selectSql = "SELECT * FROM " + quoteIdentifier(tableName_) +
                    " WHERE " + quoteIdentifier(pk) + " = " + placeholder(idx);
                std::vector<std::string> selectParams = {cond.values[0]};
                result = co_await execDynamic(selectSql, std::move(selectParams));
                break;
            }
        }
    }
    co_return result;
}

drogon::Task<drogon::orm::Result> QueryBuilder::executeDelete() {
    if (tableName_.empty()) {
        throw std::logic_error("No table specified for DELETE");
    }

    std::vector<std::string> params;
    std::ostringstream sql;

    sql << "DELETE FROM " << quoteIdentifier(tableName_);
    sql << buildWhereClause(params);
    if (!isMysql()) {
        sql << " RETURNING *";
    }

    co_return co_await execDynamic(sql.str(), std::move(params));
}

drogon::Task<size_t> QueryBuilder::executeCount() {
    if (tableName_.empty()) {
        throw std::logic_error("No table specified for COUNT");
    }

    std::vector<std::string> params;
    std::ostringstream sql;

    sql << "SELECT COUNT(*) AS count FROM " << quoteIdentifier(tableName_);
    sql << buildWhereClause(params);

    auto result = co_await execDynamic(sql.str(), std::move(params));
    if (result.size() > 0) {
        co_return result[0]["count"].as<size_t>();
    }
    co_return 0;
}

// --- Private helpers ---

void QueryBuilder::validateColumn(const std::string& column) const {
    if (!tableMeta_) {
        throw std::logic_error(
            "Cannot validate column: no table selected");
    }
    if (!tableMeta_->hasColumn(column)) {
        throw std::invalid_argument(
            "Column '" + column + "' does not exist in table '" +
            tableName_ + "'");
    }
}

bool QueryBuilder::isMysql() const {
    static const bool mysql = (EnvConfig::instance().getDbRdbms() == "mysql");
    return mysql;
}

std::string QueryBuilder::quoteIdentifier(const std::string& name) const {
    if (isMysql()) {
        return "`" + name + "`";
    }
    return "\"" + name + "\"";
}

std::string QueryBuilder::placeholder(int& idx) const {
    if (isMysql()) {
        ++idx;
        return "?";
    }
    return "$" + std::to_string(idx++);
}

std::string QueryBuilder::jsonValueToString(const Json::Value& value) const {
    if (value.isString()) {
        return value.asString();
    } else if (value.isInt64()) {
        return std::to_string(value.asInt64());
    } else if (value.isUInt64()) {
        return std::to_string(value.asUInt64());
    } else if (value.isDouble()) {
        std::ostringstream ss;
        ss << value.asDouble();
        return ss.str();
    } else if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    } else if (value.isNull()) {
        return "";
    } else {
        // For objects/arrays, serialize to JSON string
        static thread_local Json::StreamWriterBuilder writer = [](){
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return w;
        }();
        return Json::writeString(writer, value);
    }
}

std::string QueryBuilder::buildWhereClause(
    std::vector<std::string>& params) const {
    if (whereConditions_.empty()) return "";

    std::ostringstream sql;
    sql << " WHERE ";
    int placeholderIdx = 1;

    for (size_t i = 0; i < whereConditions_.size(); ++i) {
        if (i > 0) sql << " AND ";

        const auto& cond = whereConditions_[i];

        if (cond.isNullCheck) {
            sql << quoteIdentifier(cond.column) << " " << cond.op;
        } else if (cond.op == "IN") {
            sql << quoteIdentifier(cond.column) << " IN (";
            for (size_t j = 0; j < cond.values.size(); ++j) {
                if (j > 0) sql << ", ";
                sql << placeholder(placeholderIdx);
                params.push_back(cond.values[j]);
            }
            sql << ")";
        } else {
            sql << quoteIdentifier(cond.column) << " " << cond.op
                << " " << placeholder(placeholderIdx);
            params.push_back(cond.values[0]);
        }
    }

    return sql.str();
}

std::string QueryBuilder::buildOrderByClause() const {
    if (orderByClauses_.empty()) return "";

    std::ostringstream sql;
    sql << " ORDER BY ";
    for (size_t i = 0; i < orderByClauses_.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << quoteIdentifier(orderByClauses_[i].column) << " "
            << orderByClauses_[i].direction;
    }
    return sql.str();
}

std::string QueryBuilder::buildLimitOffsetClause() const {
    std::ostringstream sql;
    if (limit_.has_value()) {
        sql << " LIMIT " << *limit_;
    }
    if (offset_.has_value()) {
        sql << " OFFSET " << *offset_;
    }
    return sql.str();
}

drogon::Task<drogon::orm::Result> QueryBuilder::execDynamic(
    const std::string& sql,
    std::vector<std::string> params) {
    LOG_DEBUG << "QueryBuilder SQL: " << sql;
    const auto& constParams = params;
    co_return co_await db_->execSqlCoro(sql, constParams);
}
