// Author: Udaykiran Atta
// License: MIT

#include "schema/SchemaIntrospector.h"
#include "schema/ModelRegistry.h"
#include "schema/MySQLIntrospector.h"
#include "schema/PostgresIntrospector.h"
#include "schema/SQLiteIntrospector.h"
#include <drogon/drogon.h>
#include <algorithm>
#include <stdexcept>

std::unique_ptr<SchemaIntrospector> SchemaIntrospector::create(
    const std::string& dbEngine)
{
    if (dbEngine == "mysql")
    {
        return std::make_unique<MySQLIntrospector>();
    }
    else if (dbEngine == "postgresql")
    {
        return std::make_unique<PostgresIntrospector>();
    }
    else if (dbEngine == "sqlite3")
    {
        return std::make_unique<SQLiteIntrospector>();
    }
    else
    {
        throw std::runtime_error(
            "Unsupported database engine: " + dbEngine);
    }
}

drogon::Task<void> SchemaIntrospector::introspectSchema(
    const drogon::orm::DbClientPtr& db,
    const std::string& schema)
{
    auto tables = co_await discoverTables(db, schema);
    auto allColumns = co_await discoverAllColumns(db, schema);
    auto allPrimaryKeys = co_await discoverAllPrimaryKeys(db, schema);
    auto allForeignKeys = co_await discoverAllForeignKeys(db, schema);

    size_t totalColumns = 0;

    for (const auto& tableName : tables)
    {
        TableMeta meta;
        meta.name = tableName;
        meta.schema = schema;

        if (auto it = allColumns.find(tableName); it != allColumns.end())
        {
            meta.columns = std::move(it->second);
        }

        if (auto it = allPrimaryKeys.find(tableName);
            it != allPrimaryKeys.end())
        {
            meta.primaryKeys = std::move(it->second);
        }

        if (auto it = allForeignKeys.find(tableName);
            it != allForeignKeys.end())
        {
            meta.foreignKeys = std::move(it->second);
        }

        // Mark primary key columns
        for (auto& col : meta.columns)
        {
            if (std::find(meta.primaryKeys.begin(),
                          meta.primaryKeys.end(),
                          col.name) != meta.primaryKeys.end())
            {
                col.isPrimaryKey = true;
            }
        }

        totalColumns += meta.columns.size();

        ModelRegistry::instance().registerTable(tableName, std::move(meta));
    }

    LOG_INFO << "Schema introspection complete: " << tables.size()
             << " tables, " << totalColumns << " columns";
}
