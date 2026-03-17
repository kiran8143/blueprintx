// Author: Udaykiran Atta
// License: MIT

#include <drogon/drogon.h>
#include <iostream>
#include "config/EnvConfig.h"
#include "database/DatabaseManager.h"
#include "schema/SchemaIntrospector.h"
#include "schema/ModelRegistry.h"
#include "controllers/BlueprintController.h"
#include "openapi/OpenApiGenerator.h"
#include "cache/RedisCache.h"

int main() {
    // Step 1: Load environment configuration from .env file
    auto& config = EnvConfig::instance();
    config.load(".env");

    // Step 2: Read configuration values
    const auto port = config.getInt("PORT", 8080);
    const auto host = config.get("HOST", "0.0.0.0");
    const auto environment = config.get("ENVIRONMENT", "development");
    const auto logLevel = config.get("LOG_LEVEL", "DEBUG");

    // JWT configuration
    const auto jwtSecret = config.get("JWT_SECRET", "");

    // CORS configuration
    const auto corsOrigins = config.getList("CORS_ORIGINS");

    // Step 3: Load Drogon framework config (non-secret settings only)
    drogon::app().loadConfigFile("./config.json");

    // Step 4: Override listener from .env (takes precedence over config.json)
    drogon::app().addListener(host, static_cast<uint16_t>(port));

    // Step 5: Configure log level from .env
    if (logLevel == "TRACE") {
        trantor::Logger::setLogLevel(trantor::Logger::kTrace);
    } else if (logLevel == "DEBUG") {
        trantor::Logger::setLogLevel(trantor::Logger::kDebug);
    } else if (logLevel == "INFO") {
        trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    } else if (logLevel == "WARN") {
        trantor::Logger::setLogLevel(trantor::Logger::kWarn);
    } else if (logLevel == "ERROR") {
        trantor::Logger::setLogLevel(trantor::Logger::kError);
    } else if (logLevel == "FATAL") {
        trantor::Logger::setLogLevel(trantor::Logger::kFatal);
    }

    // Step 6: Register databases from .env (MUST be before app().run())
    DatabaseManager::registerDatabases();

    // Step 6b: Register Redis client if configured (for L2 cache)
    {
        const auto redisHost = config.get("REDIS_HOST", "");
        if (!redisHost.empty()) {
            const auto redisPort = static_cast<unsigned short>(
                config.getInt("REDIS_PORT", 6379));
            const auto redisPassword = config.get("REDIS_PASSWORD", "");
            const auto redisDb = static_cast<unsigned int>(
                config.getInt("REDIS_DB", 0));
            try {
                drogon::app().createRedisClient(
                    redisHost, redisPort, "default",
                    redisPassword, /*connectionNum=*/1,
                    /*isFast=*/false, /*timeout=*/-1.0, redisDb);
                RedisCache::instance().markRegistered();
                LOG_INFO << "Redis client registered: " << redisHost
                         << ":" << redisPort;
            } catch (const std::exception& e) {
                LOG_WARN << "Failed to register Redis client: " << e.what()
                         << " (L2 cache will be disabled)";
            }
        }
    }

    // Step 7: Register pool warming (runs after event loops start)
    DatabaseManager::warmPools();

    // Step 8: Log startup banner
    LOG_INFO << "========================================";
    LOG_INFO << "  Drogon Blueprint Framework v0.1.0";
    LOG_INFO << "========================================";
    LOG_INFO << "  Environment : " << environment;
    LOG_INFO << "  Listen      : " << host << ":" << port;
    LOG_INFO << "  Databases   : " << DatabaseManager::getRegisteredNames().size() << " configured";
    LOG_INFO << "  JWT Secret  : " << (jwtSecret.empty() ? "NOT SET (WARNING)" : "configured");
    LOG_INFO << "  CORS Origins: " << (corsOrigins.empty() ? "none" : config.get("CORS_ORIGINS"));
    LOG_INFO << "  Log Level   : " << logLevel;
    LOG_INFO << "  Architecture: C++20 coroutines (async-first)";
    LOG_INFO << "  API Docs    : http://" << host << ":" << port << "/api/docs";
    LOG_INFO << "========================================";

    if (jwtSecret.empty()) {
        LOG_WARN << "JWT_SECRET is not set! Authentication will not work.";
    }

    // Step 9: Global exception handler for uncaught coroutine exceptions
    drogon::app().setExceptionHandler(
        [](const std::exception& e,
           const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            LOG_ERROR << "Uncaught exception on " << req->path()
                      << ": " << e.what();
            Json::Value body;
            body["error"]["message"] = "Internal server error";
            body["error"]["status"] = 500;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
        });

    // Step 10: Register wildcard routes BEFORE app().run()
    // These routes check ModelRegistry at request time, so they work even
    // before schema introspection completes (returning 404 for unknown tables).
    BlueprintController::registerRoutes();
    OpenApiGenerator::registerRoutes();

    // Step 10: Schema introspection (runs after event loop + DB ready)
    // Populates ModelRegistry so the wildcard routes start serving tables.
    const auto dbRdbms = config.getDbRdbms();
    const auto dbName = config.get("DB_NAME", "mydb");
    drogon::app().registerBeginningAdvice([dbRdbms, dbName]() {
        drogon::async_run([dbRdbms, dbName]() -> drogon::Task<> {
            try {
                auto db = drogon::app().getDbClient("default");
                auto introspector = SchemaIntrospector::create(dbRdbms);

                std::string schema;
                if (dbRdbms == "mysql") {
                    schema = dbName;
                } else if (dbRdbms == "postgresql") {
                    schema = "public";
                } else {
                    schema = "main";
                }

                LOG_INFO << "Starting schema introspection for " << dbRdbms
                         << " (schema: " << schema << ")...";

                co_await introspector->introspectSchema(db, schema);

                auto& registry = ModelRegistry::instance();
                LOG_INFO << "========================================";
                LOG_INFO << "  Schema Discovery Complete";
                LOG_INFO << "  Tables : " << registry.tableCount();
                LOG_INFO << "  Columns: " << registry.totalColumnCount();
                LOG_INFO << "========================================";

                for (const auto& name : registry.getTableNames()) {
                    const auto* table = registry.getTable(name);
                    if (table) {
                        LOG_DEBUG << "  Table: " << name
                                 << " (" << table->columns.size() << " columns, "
                                 << table->primaryKeys.size() << " PKs, "
                                 << table->foreignKeys.size() << " FKs)";
                    }
                }

                LOG_INFO << "Blueprint CRUD ready for "
                         << registry.tableCount() << " tables";

            } catch (const std::exception& e) {
                LOG_ERROR << "Schema introspection failed: " << e.what();
                LOG_ERROR << "Application will continue but blueprint CRUD will not work.";
            }
        });
    });

    // Step 11: Start the async event loop (blocks until shutdown)
    LOG_INFO << "Starting Drogon event loop...";
    drogon::app().run();

    return 0;
}
