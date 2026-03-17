// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Types.h"

class SchemaIntrospector {
public:
    virtual ~SchemaIntrospector() = default;

    static std::unique_ptr<SchemaIntrospector> create(
        const std::string& dbEngine);

    virtual drogon::Task<std::vector<std::string>> discoverTables(
        const drogon::orm::DbClientPtr& db,
        const std::string& schema) = 0;

    virtual drogon::Task<
        std::unordered_map<std::string, std::vector<ColumnMeta>>>
    discoverAllColumns(const drogon::orm::DbClientPtr& db,
                       const std::string& schema) = 0;

    virtual drogon::Task<
        std::unordered_map<std::string, std::vector<std::string>>>
    discoverAllPrimaryKeys(const drogon::orm::DbClientPtr& db,
                           const std::string& schema) = 0;

    virtual drogon::Task<
        std::unordered_map<std::string, std::vector<ForeignKeyMeta>>>
    discoverAllForeignKeys(const drogon::orm::DbClientPtr& db,
                           const std::string& schema) = 0;

    virtual drogon::Task<void> introspectSchema(
        const drogon::orm::DbClientPtr& db,
        const std::string& schema);
};
