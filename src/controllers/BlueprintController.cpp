// Author: Udaykiran Atta
// License: MIT

#include "controllers/BlueprintController.h"
#include "api/ApiResponse.h"
#include "query/QueryBuilder.h"
#include "schema/ModelRegistry.h"
#include "schema/JsonSerializer.h"
#include "schema/RequestValidator.h"
#include "protection/FieldGuard.h"
#include "protection/AuditInjector.h"
#include "protection/CodeGenerator.h"
#include "cache/CacheManager.h"

#include <algorithm>

void BlueprintController::registerRoutes() {
    // Register wildcard routes BEFORE app().run().
    // Table name is extracted from path param {1} at request time.
    // ModelRegistry is checked dynamically — tables appear once introspection completes.

    // GET /api/v1/{table} → list
    drogon::app().registerHandler(
        "/api/v1/{1}",
        [](drogon::HttpRequestPtr req, std::string table)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleList(req, std::move(table));
        },
        {drogon::Get});

    // POST /api/v1/{table} → create
    drogon::app().registerHandler(
        "/api/v1/{1}",
        [](drogon::HttpRequestPtr req, std::string table)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleCreate(req, std::move(table));
        },
        {drogon::Post});

    // POST /api/v1/{table}/bulk → bulk create
    drogon::app().registerHandler(
        "/api/v1/{1}/bulk",
        [](drogon::HttpRequestPtr req, std::string table)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleBulkCreate(req, std::move(table));
        },
        {drogon::Post});

    // GET /api/v1/{table}/{id} → get by id
    drogon::app().registerHandler(
        "/api/v1/{1}/{2}",
        [](drogon::HttpRequestPtr req, std::string table, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleGetById(req, std::move(table), std::move(id));
        },
        {drogon::Get});

    // PUT /api/v1/{table}/{id} → update
    drogon::app().registerHandler(
        "/api/v1/{1}/{2}",
        [](drogon::HttpRequestPtr req, std::string table, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleUpdate(req, std::move(table), std::move(id));
        },
        {drogon::Put});

    // DELETE /api/v1/{table}/{id} → delete
    drogon::app().registerHandler(
        "/api/v1/{1}/{2}",
        [](drogon::HttpRequestPtr req, std::string table, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await handleDelete(req, std::move(table), std::move(id));
        },
        {drogon::Delete});

    LOG_INFO << "Blueprint wildcard routes registered at /api/v1/{table}[/{id}]";
}

// --- CRUD Handlers ---

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleList(
    drogon::HttpRequestPtr req, std::string tableName) {
    try {
        auto& cache = CacheManager::instance();
        auto cached = co_await cache.get(req);
        if (cached) {
            co_return ApiResponse::ok(*cached);
        }

        auto db = getDb();
        auto params = parseListParams(req);
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        // Build query with pagination, filtering, sorting
        QueryBuilder qb(db);
        qb.table(tableName);

        // Apply filters
        for (const auto& [col, val] : params.filters) {
            if (meta->hasColumn(col)) {
                qb.where(col, Json::Value(val));
            }
        }

        // Apply sorting
        if (!params.sortColumn.empty() && meta->hasColumn(params.sortColumn)) {
            qb.orderBy(params.sortColumn, params.sortDirection);
        }

        // Apply pagination — fetch limit+1 rows so we can detect has_more
        // without a separate COUNT query on first-page requests.
        qb.limit(params.limit + 1);
        qb.offset(params.offset);

        auto result = co_await qb.executeSelect();

        // Determine if there are more rows beyond this page
        bool hasMore = (result.size() > params.limit);

        // Serialize only up to `limit` rows (drop the extra probe row)
        Json::Value data(Json::arrayValue);
        size_t rowsToSerialize = hasMore ? params.limit : result.size();
        for (size_t i = 0; i < rowsToSerialize; ++i) {
            data.append(JsonSerializer::serializeRow(result[i], *meta));
        }

        // Only run COUNT query when the client explicitly requests it
        // (count=true) or when offset > 0 (needs accurate total for UI).
        // For first-page requests this eliminates a full DB round-trip.
        auto countParam = req->getParameter("count");
        bool wantCount = (params.offset > 0) || (countParam == "true");

        size_t total;
        if (wantCount) {
            QueryBuilder countQb(db);
            countQb.table(tableName);
            for (const auto& [col, val] : params.filters) {
                if (meta->hasColumn(col)) {
                    countQb.where(col, Json::Value(val));
                }
            }
            total = co_await countQb.executeCount();
        } else if (!hasMore) {
            // We have all the rows — exact total is known
            total = params.offset + result.size();
        } else {
            // First page with more rows — total unknown, signal with -1
            total = static_cast<size_t>(-1);
        }

        // Build the response JSON for caching
        Json::Value responseData;
        responseData["data"] = data;
        Json::Value metaJson;
        if (total == static_cast<size_t>(-1)) {
            metaJson["total"] = -1;
        } else {
            metaJson["total"] = static_cast<Json::UInt64>(total);
        }
        metaJson["limit"] = static_cast<Json::UInt64>(params.limit);
        metaJson["offset"] = static_cast<Json::UInt64>(params.offset);
        metaJson["has_more"] = hasMore;
        responseData["meta"] = metaJson;
        co_await cache.put(req, responseData);
        co_return ApiResponse::ok(responseData);

    } catch (const std::exception& e) {
        LOG_ERROR << "List " << tableName << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleGetById(
    drogon::HttpRequestPtr req, std::string tableName, std::string id) {
    try {
        auto& cache = CacheManager::instance();
        auto cached = co_await cache.get(req);
        if (cached) {
            co_return ApiResponse::ok(*cached);
        }

        auto db = getDb();
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        if (meta->primaryKeys.empty()) {
            co_return ApiResponse::badRequest(
                "Table '" + tableName + "' has no primary key");
        }

        const std::string& pkColumn = meta->primaryKeys[0];

        QueryBuilder qb(db);
        auto result = co_await qb.table(tableName)
            .where(pkColumn, Json::Value(id))
            .limit(1)
            .executeSelect();

        if (result.size() == 0) {
            co_return ApiResponse::notFound(
                tableName + " with id '" + id + "' not found");
        }

        auto data = JsonSerializer::serializeRow(result[0], *meta);
        co_await cache.put(req, data);
        co_return ApiResponse::ok(data);

    } catch (const std::exception& e) {
        LOG_ERROR << "Get " << tableName << "/" << id << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleCreate(
    drogon::HttpRequestPtr req, std::string tableName) {
    try {
        auto db = getDb();
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        auto body = req->getJsonObject();
        if (!body || !body->isObject()) {
            co_return ApiResponse::badRequest("Request body must be a JSON object");
        }

        // Validate request
        auto errors = RequestValidator::validateCreate(*body, *meta);
        if (!errors.empty()) {
            co_return ApiResponse::validationError(
                RequestValidator::errorsToJson(errors));
        }

        // Protection layer: sanitize, inject audit fields, inject code
        auto sanitized = FieldGuard::sanitize(tableName, *body);
        auto userId = req->attributes()->get<std::string>("user_id");
        AuditInjector::injectCreateWithMeta(sanitized, userId, *meta);
        CodeGenerator::injectCode(sanitized, *meta);

        QueryBuilder qb(db);
        auto result = co_await qb.table(tableName).executeInsert(sanitized);

        co_await CacheManager::instance().invalidateTable(tableName);

        if (result.size() > 0) {
            auto data = JsonSerializer::serializeRow(result[0], *meta);
            co_return ApiResponse::created(data);
        }

        // Fallback for MySQL/SQLite: query the inserted row
        Json::Value inserted;
        inserted["message"] = "Record created";
        co_return ApiResponse::created(inserted);

    } catch (const drogon::orm::DrogonDbException& e) {
        std::string msg = e.base().what();
        if (msg.find("duplicate") != std::string::npos ||
            msg.find("unique") != std::string::npos ||
            msg.find("Duplicate") != std::string::npos ||
            msg.find("UNIQUE") != std::string::npos) {
            co_return ApiResponse::conflict("Record already exists");
        }
        LOG_ERROR << "Create " << tableName << " failed: " << msg;
        co_return ApiResponse::internalError(msg);
    } catch (const std::exception& e) {
        LOG_ERROR << "Create " << tableName << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleUpdate(
    drogon::HttpRequestPtr req, std::string tableName, std::string id) {
    try {
        auto db = getDb();
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        if (meta->primaryKeys.empty()) {
            co_return ApiResponse::badRequest(
                "Table '" + tableName + "' has no primary key");
        }

        auto body = req->getJsonObject();
        if (!body || !body->isObject()) {
            co_return ApiResponse::badRequest("Request body must be a JSON object");
        }

        // Validate request
        auto errors = RequestValidator::validateUpdate(*body, *meta);
        if (!errors.empty()) {
            co_return ApiResponse::validationError(
                RequestValidator::errorsToJson(errors));
        }

        // Protection layer: sanitize, inject audit fields
        auto sanitized = FieldGuard::sanitize(tableName, *body);
        auto userId = req->attributes()->get<std::string>("user_id");
        AuditInjector::injectUpdateWithMeta(sanitized, userId, *meta);

        const std::string& pkColumn = meta->primaryKeys[0];

        QueryBuilder qb(db);
        auto result = co_await qb.table(tableName)
            .where(pkColumn, Json::Value(id))
            .executeUpdate(sanitized);

        co_await CacheManager::instance().invalidateTable(tableName);

        if (result.size() > 0) {
            auto data = JsonSerializer::serializeRow(result[0], *meta);
            co_return ApiResponse::ok(data);
        }

        co_return ApiResponse::notFound(
            tableName + " with id '" + id + "' not found");

    } catch (const std::exception& e) {
        LOG_ERROR << "Update " << tableName << "/" << id << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleDelete(
    drogon::HttpRequestPtr req, std::string tableName, std::string id) {
    try {
        auto db = getDb();
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        if (meta->primaryKeys.empty()) {
            co_return ApiResponse::badRequest(
                "Table '" + tableName + "' has no primary key");
        }

        const std::string& pkColumn = meta->primaryKeys[0];

        QueryBuilder qb(db);
        auto result = co_await qb.table(tableName)
            .where(pkColumn, Json::Value(id))
            .executeDelete();

        co_await CacheManager::instance().invalidateTable(tableName);

        if (result.size() > 0) {
            auto data = JsonSerializer::serializeRow(result[0], *meta);
            co_return ApiResponse::ok(data);
        }

        co_return ApiResponse::notFound(
            tableName + " with id '" + id + "' not found");

    } catch (const std::exception& e) {
        LOG_ERROR << "Delete " << tableName << "/" << id << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

drogon::Task<drogon::HttpResponsePtr> BlueprintController::handleBulkCreate(
    drogon::HttpRequestPtr req, std::string tableName) {
    try {
        auto db = getDb();
        auto& registry = ModelRegistry::instance();
        const auto* meta = registry.getTable(tableName);
        if (!meta) {
            co_return ApiResponse::notFound("Table '" + tableName + "' not found");
        }

        auto body = req->getJsonObject();
        if (!body || !body->isArray() || body->empty()) {
            co_return ApiResponse::badRequest(
                "Bulk create requires a non-empty JSON array");
        }

        Json::Value results(Json::arrayValue);
        Json::Value errors(Json::arrayValue);

        for (Json::ArrayIndex i = 0; i < body->size(); ++i) {
            const auto& item = (*body)[i];
            if (!item.isObject()) {
                Json::Value err;
                err["index"] = i;
                err["message"] = "Item must be a JSON object";
                errors.append(err);
                continue;
            }

            auto validationErrors = RequestValidator::validateCreate(item, *meta);
            if (!validationErrors.empty()) {
                Json::Value err;
                err["index"] = i;
                err["errors"] = RequestValidator::errorsToJson(validationErrors);
                errors.append(err);
                continue;
            }

            // Protection layer: sanitize, inject audit fields, inject code
            auto sanitized = FieldGuard::sanitize(tableName, item);
            auto userId = req->attributes()->get<std::string>("user_id");
            AuditInjector::injectCreateWithMeta(sanitized, userId, *meta);
            CodeGenerator::injectCode(sanitized, *meta);

            try {
                QueryBuilder qb(db);
                auto result = co_await qb.table(tableName).executeInsert(sanitized);
                if (result.size() > 0) {
                    results.append(
                        JsonSerializer::serializeRow(result[0], *meta));
                }
            } catch (const std::exception& e) {
                Json::Value err;
                err["index"] = i;
                err["message"] = e.what();
                errors.append(err);
            }
        }

        co_await CacheManager::instance().invalidateTable(tableName);

        Json::Value response;
        response["created"] = results;
        response["errors"] = errors;
        response["total_submitted"] = body->size();
        response["total_created"] = results.size();

        if (errors.empty()) {
            co_return ApiResponse::created(response);
        }
        co_return ApiResponse::ok(response);

    } catch (const std::exception& e) {
        LOG_ERROR << "Bulk create " << tableName << " failed: " << e.what();
        co_return ApiResponse::internalError(e.what());
    }
}

// --- Helpers ---

BlueprintController::ListParams BlueprintController::parseListParams(
    const drogon::HttpRequestPtr& req) {
    ListParams params;

    // Pagination: ?limit=20&offset=0
    auto limitStr = req->getParameter("limit");
    auto offsetStr = req->getParameter("offset");
    if (!limitStr.empty()) {
        try { params.limit = std::stoul(limitStr); } catch (...) {}
        if (params.limit > 50000) params.limit = 50000; // max cap
        if (params.limit == 0) params.limit = 20;
    }
    if (!offsetStr.empty()) {
        try { params.offset = std::stoul(offsetStr); } catch (...) {}
    }

    // Sorting: ?sort=created_at:desc
    auto sortStr = req->getParameter("sort");
    if (!sortStr.empty()) {
        auto colonPos = sortStr.find(':');
        if (colonPos != std::string::npos) {
            params.sortColumn = sortStr.substr(0, colonPos);
            std::string dir = sortStr.substr(colonPos + 1);
            std::transform(dir.begin(), dir.end(), dir.begin(), ::toupper);
            if (dir == "DESC" || dir == "ASC") {
                params.sortDirection = dir;
            }
        } else {
            params.sortColumn = sortStr;
        }
    }

    // Filtering: ?filter[status]=active&filter[role]=admin
    for (const auto& [key, val] : req->getParameters()) {
        if (key.size() > 8 && key.substr(0, 7) == "filter[") {
            auto closeBracket = key.find(']', 7);
            if (closeBracket != std::string::npos) {
                std::string colName = key.substr(7, closeBracket - 7);
                params.filters.emplace_back(colName, val);
            }
        }
    }

    return params;
}

drogon::orm::DbClientPtr BlueprintController::getDb() {
    return drogon::app().getDbClient("default");
}
