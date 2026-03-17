// Author: Udaykiran Atta
// License: MIT

#include "schema/PostgresIntrospector.h"
#include "schema/TypeMapper.h"
#include <drogon/drogon.h>

drogon::Task<std::vector<std::string>> PostgresIntrospector::discoverTables(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    const std::string sql =
        "SELECT table_name "
        "FROM information_schema.tables "
        "WHERE table_schema = $1 AND table_type = 'BASE TABLE' "
        "ORDER BY table_name";

    auto result = co_await db->execSqlCoro(sql, schema);

    std::vector<std::string> tables;
    tables.reserve(result.size());
    for (const auto& row : result) {
        tables.emplace_back(row["table_name"].as<std::string>());
    }
    co_return tables;
}

drogon::Task<std::unordered_map<std::string, std::vector<ColumnMeta>>>
PostgresIntrospector::discoverAllColumns(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    const std::string sql =
        "SELECT table_name, column_name, ordinal_position, column_default, "
        "is_nullable, data_type, udt_name, character_maximum_length, "
        "numeric_precision, numeric_scale, is_identity "
        "FROM information_schema.columns "
        "WHERE table_schema = $1 "
        "ORDER BY table_name, ordinal_position";

    auto result = co_await db->execSqlCoro(sql, schema);

    std::unordered_map<std::string, std::vector<ColumnMeta>> allColumns;

    for (const auto& row : result) {
        std::string tableName = row["table_name"].as<std::string>();
        std::string columnName = row["column_name"].as<std::string>();
        std::string dataType = row["data_type"].as<std::string>();
        std::string udtName = row["udt_name"].as<std::string>();
        std::string isNullable = row["is_nullable"].as<std::string>();

        ColumnMeta col;
        col.name = columnName;
        col.rawType = udtName;
        col.ordinalPosition = row["ordinal_position"].as<int>();
        col.isNullable = (isNullable == "YES");

        auto mapping = TypeMapper::mapPostgresType(dataType, udtName);
        col.sqlType = mapping.sqlType;
        col.jsonType = mapping.jsonType;

        if (!row["column_default"].isNull()) {
            std::string columnDefault = row["column_default"].as<std::string>();
            col.defaultValue = columnDefault;

            // Auto-increment detection: nextval() sequences
            if (columnDefault.rfind("nextval(", 0) == 0) {
                col.isAutoIncrement = true;
            }
        }

        // Auto-increment detection: identity columns
        if (!row["is_identity"].isNull()) {
            std::string isIdentity = row["is_identity"].as<std::string>();
            if (isIdentity == "YES") {
                col.isAutoIncrement = true;
            }
        }

        if (!row["character_maximum_length"].isNull()) {
            col.maxLength = row["character_maximum_length"].as<int>();
        }
        if (!row["numeric_precision"].isNull()) {
            col.precision = row["numeric_precision"].as<int>();
        }
        if (!row["numeric_scale"].isNull()) {
            col.scale = row["numeric_scale"].as<int>();
        }

        allColumns[tableName].emplace_back(std::move(col));
    }

    co_return allColumns;
}

drogon::Task<std::unordered_map<std::string, std::vector<std::string>>>
PostgresIntrospector::discoverAllPrimaryKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    const std::string sql =
        "SELECT tc.table_name, kcu.column_name, kcu.ordinal_position "
        "FROM information_schema.table_constraints AS tc "
        "JOIN information_schema.key_column_usage AS kcu "
        "ON tc.constraint_name = kcu.constraint_name "
        "AND tc.table_schema = kcu.table_schema "
        "WHERE tc.table_schema = $1 AND tc.constraint_type = 'PRIMARY KEY' "
        "ORDER BY tc.table_name, kcu.ordinal_position";

    auto result = co_await db->execSqlCoro(sql, schema);

    std::unordered_map<std::string, std::vector<std::string>> allPrimaryKeys;

    for (const auto& row : result) {
        std::string tableName = row["table_name"].as<std::string>();
        std::string columnName = row["column_name"].as<std::string>();
        allPrimaryKeys[tableName].emplace_back(std::move(columnName));
    }

    co_return allPrimaryKeys;
}

drogon::Task<std::unordered_map<std::string, std::vector<ForeignKeyMeta>>>
PostgresIntrospector::discoverAllForeignKeys(
    const drogon::orm::DbClientPtr& db, const std::string& schema)
{
    const std::string sql =
        "SELECT tc.table_name, kcu.column_name, tc.constraint_name, "
        "ccu.table_name AS referenced_table_name, "
        "ccu.column_name AS referenced_column_name "
        "FROM information_schema.table_constraints AS tc "
        "JOIN information_schema.key_column_usage AS kcu "
        "ON tc.constraint_name = kcu.constraint_name "
        "AND tc.table_schema = kcu.table_schema "
        "JOIN information_schema.constraint_column_usage AS ccu "
        "ON tc.constraint_name = ccu.constraint_name "
        "AND tc.table_schema = ccu.table_schema "
        "WHERE tc.table_schema = $1 AND tc.constraint_type = 'FOREIGN KEY' "
        "ORDER BY tc.table_name, tc.constraint_name";

    auto result = co_await db->execSqlCoro(sql, schema);

    std::unordered_map<std::string, std::vector<ForeignKeyMeta>> allForeignKeys;

    for (const auto& row : result) {
        std::string tableName = row["table_name"].as<std::string>();

        ForeignKeyMeta fk;
        fk.columnName = row["column_name"].as<std::string>();
        fk.constraintName = row["constraint_name"].as<std::string>();
        fk.referencedTable = row["referenced_table_name"].as<std::string>();
        fk.referencedColumn = row["referenced_column_name"].as<std::string>();

        allForeignKeys[tableName].emplace_back(std::move(fk));
    }

    co_return allForeignKeys;
}
