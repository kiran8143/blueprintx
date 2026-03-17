// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>
#include <string>
#include <vector>
#include <optional>
#include "schema/Types.h"

struct WhereCondition {
    std::string column;
    std::string op;       // =, !=, <, >, <=, >=, LIKE, IN, IS NULL, IS NOT NULL
    std::vector<std::string> values; // empty for IS NULL / IS NOT NULL
    bool isNullCheck = false;
};

struct OrderByClause {
    std::string column;
    std::string direction; // ASC or DESC
};

class QueryBuilder {
public:
    explicit QueryBuilder(const drogon::orm::DbClientPtr& db);

    // Table selection (validates against ModelRegistry)
    QueryBuilder& table(const std::string& tableName);

    // SELECT columns (empty = SELECT *)
    QueryBuilder& select(const std::vector<std::string>& columns);

    // WHERE conditions
    QueryBuilder& where(const std::string& column, const std::string& op,
                         const Json::Value& value);
    QueryBuilder& where(const std::string& column, const Json::Value& value);
    QueryBuilder& whereNull(const std::string& column);
    QueryBuilder& whereNotNull(const std::string& column);
    QueryBuilder& whereIn(const std::string& column,
                           const Json::Value& values);

    // ORDER BY
    QueryBuilder& orderBy(const std::string& column,
                           const std::string& direction = "ASC");

    // LIMIT / OFFSET
    QueryBuilder& limit(size_t count);
    QueryBuilder& offset(size_t count);

    // Execute operations (coroutine)
    drogon::Task<drogon::orm::Result> executeSelect();
    drogon::Task<drogon::orm::Result> executeInsert(const Json::Value& data);
    drogon::Task<drogon::orm::Result> executeUpdate(const Json::Value& data);
    drogon::Task<drogon::orm::Result> executeDelete();
    drogon::Task<size_t> executeCount();

    // Reset builder for reuse
    QueryBuilder& reset();

    // Access table metadata (set after table() call)
    const TableMeta* getTableMeta() const { return tableMeta_; }
    const std::string& getTableName() const { return tableName_; }

private:
    drogon::orm::DbClientPtr db_;
    std::string tableName_;
    const TableMeta* tableMeta_ = nullptr;
    std::vector<std::string> selectColumns_;
    std::vector<WhereCondition> whereConditions_;
    std::vector<OrderByClause> orderByClauses_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;

    void validateColumn(const std::string& column) const;
    std::string quoteIdentifier(const std::string& name) const;
    std::string placeholder(int& idx) const;
    std::string jsonValueToString(const Json::Value& value) const;
    bool isMysql() const;

    // SQL building helpers (populate params vector)
    std::string buildWhereClause(std::vector<std::string>& params) const;
    std::string buildOrderByClause() const;
    std::string buildLimitOffsetClause() const;

    // Execute with dynamic parameters via Drogon's vector-based execSqlCoro
    drogon::Task<drogon::orm::Result> execDynamic(const std::string& sql,
                                                    std::vector<std::string> params);
};
