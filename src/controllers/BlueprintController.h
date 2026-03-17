// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>
#include <string>

// BlueprintController registers wildcard REST endpoints that dynamically
// serve any table discovered by SchemaIntrospector at runtime.
//
// Routes (registered BEFORE app().run()):
//   GET    /api/v1/{table}        → list with pagination/filter/sort
//   POST   /api/v1/{table}        → create record
//   POST   /api/v1/{table}/bulk   → bulk insert
//   GET    /api/v1/{table}/{id}   → get by primary key
//   PUT    /api/v1/{table}/{id}   → update record
//   DELETE /api/v1/{table}/{id}   → delete record
//
// ModelRegistry is checked at request time, so tables become available
// once schema introspection completes (in registerBeginningAdvice).

class BlueprintController {
public:
    // Register wildcard routes. Call BEFORE drogon::app().run().
    static void registerRoutes();

private:
    // CRUD handlers — table name extracted from path at request time
    static drogon::Task<drogon::HttpResponsePtr> handleList(
        drogon::HttpRequestPtr req, std::string tableName);

    static drogon::Task<drogon::HttpResponsePtr> handleGetById(
        drogon::HttpRequestPtr req, std::string tableName, std::string id);

    static drogon::Task<drogon::HttpResponsePtr> handleCreate(
        drogon::HttpRequestPtr req, std::string tableName);

    static drogon::Task<drogon::HttpResponsePtr> handleUpdate(
        drogon::HttpRequestPtr req, std::string tableName, std::string id);

    static drogon::Task<drogon::HttpResponsePtr> handleDelete(
        drogon::HttpRequestPtr req, std::string tableName, std::string id);

    static drogon::Task<drogon::HttpResponsePtr> handleBulkCreate(
        drogon::HttpRequestPtr req, std::string tableName);

    // Query parameter parsing helpers
    struct ListParams {
        size_t limit = 20;
        size_t offset = 0;
        std::string sortColumn;
        std::string sortDirection = "ASC";
        std::vector<std::pair<std::string, std::string>> filters;
    };

    static ListParams parseListParams(const drogon::HttpRequestPtr& req);

    static drogon::orm::DbClientPtr getDb();
};
