// Author: Udaykiran Atta
// License: MIT

#include "schema/MySQLIntrospector.h"
#include "schema/TypeMapper.h"
#include <drogon/drogon.h>

drogon::Task<std::vector<std::string>> MySQLIntrospector::discoverTables(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto result = co_await db->execSqlCoro(
        "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES "
        "WHERE TABLE_SCHEMA = ? AND TABLE_TYPE = 'BASE TABLE' "
        "ORDER BY TABLE_NAME",
        schema);

    std::vector<std::string> tables;
    tables.reserve(result.size());
    for (const auto& row : result)
    {
        tables.emplace_back(row["TABLE_NAME"].as<std::string>());
    }
    co_return tables;
}

drogon::Task<std::unordered_map<std::string, std::vector<ColumnMeta>>>
MySQLIntrospector::discoverAllColumns(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto result = co_await db->execSqlCoro(
        "SELECT TABLE_NAME, COLUMN_NAME, ORDINAL_POSITION, "
        "COLUMN_DEFAULT, IS_NULLABLE, DATA_TYPE, "
        "CHARACTER_MAXIMUM_LENGTH, NUMERIC_PRECISION, "
        "NUMERIC_SCALE, COLUMN_TYPE, EXTRA "
        "FROM INFORMATION_SCHEMA.COLUMNS "
        "WHERE TABLE_SCHEMA = ? "
        "ORDER BY TABLE_NAME, ORDINAL_POSITION",
        schema);

    std::unordered_map<std::string, std::vector<ColumnMeta>> allColumns;

    for (const auto& row : result)
    {
        auto tableName = row["TABLE_NAME"].as<std::string>();
        auto dataType = row["DATA_TYPE"].as<std::string>();
        auto columnType = row["COLUMN_TYPE"].as<std::string>();
        auto extra = row["EXTRA"].as<std::string>();

        ColumnMeta col;
        col.name = row["COLUMN_NAME"].as<std::string>();
        col.rawType = columnType;
        col.ordinalPosition =
            row["ORDINAL_POSITION"].as<int>();
        col.isNullable =
            (row["IS_NULLABLE"].as<std::string>() == "YES");
        col.isAutoIncrement =
            (extra.find("auto_increment") != std::string::npos);

        auto mapping = TypeMapper::mapMySQLType(dataType, columnType);
        col.sqlType = mapping.sqlType;
        col.jsonType = mapping.jsonType;

        if (!row["COLUMN_DEFAULT"].isNull())
        {
            col.defaultValue = row["COLUMN_DEFAULT"].as<std::string>();
        }

        if (!row["CHARACTER_MAXIMUM_LENGTH"].isNull())
        {
            col.maxLength =
                row["CHARACTER_MAXIMUM_LENGTH"].as<int>();
        }

        if (!row["NUMERIC_PRECISION"].isNull())
        {
            col.precision = row["NUMERIC_PRECISION"].as<int>();
        }

        if (!row["NUMERIC_SCALE"].isNull())
        {
            col.scale = row["NUMERIC_SCALE"].as<int>();
        }

        allColumns[tableName].emplace_back(std::move(col));
    }

    co_return allColumns;
}

drogon::Task<std::unordered_map<std::string, std::vector<std::string>>>
MySQLIntrospector::discoverAllPrimaryKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto result = co_await db->execSqlCoro(
        "SELECT TABLE_NAME, COLUMN_NAME "
        "FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE "
        "WHERE TABLE_SCHEMA = ? AND CONSTRAINT_NAME = 'PRIMARY' "
        "ORDER BY TABLE_NAME, ORDINAL_POSITION",
        schema);

    std::unordered_map<std::string, std::vector<std::string>> allPKs;

    for (const auto& row : result)
    {
        auto tableName = row["TABLE_NAME"].as<std::string>();
        allPKs[tableName].emplace_back(
            row["COLUMN_NAME"].as<std::string>());
    }

    co_return allPKs;
}

drogon::Task<std::unordered_map<std::string, std::vector<ForeignKeyMeta>>>
MySQLIntrospector::discoverAllForeignKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    auto result = co_await db->execSqlCoro(
        "SELECT TABLE_NAME, COLUMN_NAME, CONSTRAINT_NAME, "
        "REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME "
        "FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE "
        "WHERE TABLE_SCHEMA = ? AND REFERENCED_TABLE_NAME IS NOT NULL "
        "ORDER BY TABLE_NAME, CONSTRAINT_NAME",
        schema);

    std::unordered_map<std::string, std::vector<ForeignKeyMeta>> allFKs;

    for (const auto& row : result)
    {
        auto tableName = row["TABLE_NAME"].as<std::string>();

        ForeignKeyMeta fk;
        fk.constraintName = row["CONSTRAINT_NAME"].as<std::string>();
        fk.columnName = row["COLUMN_NAME"].as<std::string>();
        fk.referencedTable =
            row["REFERENCED_TABLE_NAME"].as<std::string>();
        fk.referencedColumn =
            row["REFERENCED_COLUMN_NAME"].as<std::string>();

        allFKs[tableName].emplace_back(std::move(fk));
    }

    co_return allFKs;
}
