// Author: Udaykiran Atta
// License: MIT

#pragma once
#include "SchemaIntrospector.h"

class SQLiteIntrospector : public SchemaIntrospector {
public:
    drogon::Task<std::vector<std::string>> discoverTables(
        const drogon::orm::DbClientPtr& db, const std::string& schema) override;
    drogon::Task<std::unordered_map<std::string, std::vector<ColumnMeta>>> discoverAllColumns(
        const drogon::orm::DbClientPtr& db, const std::string& schema) override;
    drogon::Task<std::unordered_map<std::string, std::vector<std::string>>> discoverAllPrimaryKeys(
        const drogon::orm::DbClientPtr& db, const std::string& schema) override;
    drogon::Task<std::unordered_map<std::string, std::vector<ForeignKeyMeta>>> discoverAllForeignKeys(
        const drogon::orm::DbClientPtr& db, const std::string& schema) override;

private:
    static bool isValidTableName(const std::string& name);
};
