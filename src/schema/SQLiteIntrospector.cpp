// Author: Udaykiran Atta
// License: MIT

#include "schema/SQLiteIntrospector.h"
#include "schema/TypeMapper.h"
#include <drogon/drogon.h>
#include <stdexcept>

bool SQLiteIntrospector::isValidTableName(const std::string& name)
{
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

drogon::Task<std::vector<std::string>> SQLiteIntrospector::discoverTables(
    const drogon::orm::DbClientPtr& db, const std::string& /*schema*/)
{
    const std::string sql =
        "SELECT name FROM sqlite_master "
        "WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
        "ORDER BY name";

    auto result = co_await db->execSqlCoro(sql);

    std::vector<std::string> tables;
    tables.reserve(result.size());
    for (const auto& row : result) {
        tables.emplace_back(row["name"].as<std::string>());
    }
    co_return tables;
}

drogon::Task<std::unordered_map<std::string, std::vector<ColumnMeta>>>
SQLiteIntrospector::discoverAllColumns(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto tables = co_await discoverTables(db, schema);

    std::unordered_map<std::string, std::vector<ColumnMeta>> allColumns;

    for (const auto& tableName : tables) {
        if (!isValidTableName(tableName)) {
            LOG_WARN << "Skipping table with invalid name: " << tableName;
            continue;
        }

        std::string pragmaSql = "PRAGMA table_info('" + tableName + "')";
        auto result = co_await db->execSqlCoro(pragmaSql);

        std::vector<ColumnMeta> columns;
        columns.reserve(result.size());

        for (const auto& row : result) {
            ColumnMeta col;
            col.name = row["name"].as<std::string>();
            col.ordinalPosition = row["cid"].as<int>();

            std::string rawType = row["type"].as<std::string>();
            col.rawType = rawType;

            auto mapping = TypeMapper::mapSQLiteType(rawType);
            col.sqlType = mapping.sqlType;
            col.jsonType = mapping.jsonType;

            int notnull = row["notnull"].as<int>();
            col.isNullable = (notnull == 0);

            if (!row["dflt_value"].isNull()) {
                col.defaultValue = row["dflt_value"].as<std::string>();
            }

            // SQLite auto-increment: INTEGER PRIMARY KEY is the rowid alias
            int pk = row["pk"].as<int>();
            if (pk > 0) {
                col.isPrimaryKey = true;
                // SQLite INTEGER PRIMARY KEY is auto-increment by nature
                auto upperType = rawType;
                std::transform(upperType.begin(), upperType.end(),
                               upperType.begin(),
                               [](unsigned char c) { return std::toupper(c); });
                if (upperType == "INTEGER") {
                    col.isAutoIncrement = true;
                }
            }

            columns.emplace_back(std::move(col));
        }

        allColumns[tableName] = std::move(columns);
    }

    co_return allColumns;
}

drogon::Task<std::unordered_map<std::string, std::vector<std::string>>>
SQLiteIntrospector::discoverAllPrimaryKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto tables = co_await discoverTables(db, schema);

    std::unordered_map<std::string, std::vector<std::string>> allPrimaryKeys;

    for (const auto& tableName : tables) {
        if (!isValidTableName(tableName)) {
            LOG_WARN << "Skipping table with invalid name: " << tableName;
            continue;
        }

        std::string pragmaSql = "PRAGMA table_info('" + tableName + "')";
        auto result = co_await db->execSqlCoro(pragmaSql);

        // Collect PK columns ordered by their pk value (composite PK order)
        std::vector<std::pair<int, std::string>> pkColumns;

        for (const auto& row : result) {
            int pk = row["pk"].as<int>();
            if (pk > 0) {
                pkColumns.emplace_back(
                    pk, row["name"].as<std::string>());
            }
        }

        // Sort by pk value to preserve composite primary key order
        std::sort(pkColumns.begin(), pkColumns.end(),
                  [](const auto& a, const auto& b) {
                      return a.first < b.first;
                  });

        if (!pkColumns.empty()) {
            std::vector<std::string> keys;
            keys.reserve(pkColumns.size());
            for (auto& [order, name] : pkColumns) {
                keys.emplace_back(std::move(name));
            }
            allPrimaryKeys[tableName] = std::move(keys);
        }
    }

    co_return allPrimaryKeys;
}

drogon::Task<std::unordered_map<std::string, std::vector<ForeignKeyMeta>>>
SQLiteIntrospector::discoverAllForeignKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto tables = co_await discoverTables(db, schema);

    std::unordered_map<std::string, std::vector<ForeignKeyMeta>> allForeignKeys;

    for (const auto& tableName : tables) {
        if (!isValidTableName(tableName)) {
            LOG_WARN << "Skipping table with invalid name: " << tableName;
            continue;
        }

        std::string pragmaSql =
            "PRAGMA foreign_key_list('" + tableName + "')";
        auto result = co_await db->execSqlCoro(pragmaSql);

        std::vector<ForeignKeyMeta> foreignKeys;

        for (const auto& row : result) {
            ForeignKeyMeta fk;
            fk.columnName = row["from"].as<std::string>();
            fk.referencedTable = row["table"].as<std::string>();
            fk.referencedColumn = row["to"].as<std::string>();

            int id = row["id"].as<int>();
            fk.constraintName = "fk_" + std::to_string(id);

            foreignKeys.emplace_back(std::move(fk));
        }

        if (!foreignKeys.empty()) {
            allForeignKeys[tableName] = std::move(foreignKeys);
        }
    }

    co_return allForeignKeys;
}
