// Author: Udaykiran Atta
// License: MIT

#include "database/DatabaseManager.h"
#include "config/EnvConfig.h"

#include <drogon/drogon.h>
#include <drogon/orm/DbConfig.h>
#include <thread>

std::vector<std::string> DatabaseManager::registeredNames_;

namespace {

drogon::orm::DbConfig makeDbConfig(
    const std::string& engine,
    const std::string& host, int port,
    const std::string& dbName, const std::string& user,
    const std::string& password, int poolSize,
    bool fastMode, double timeout, const std::string& name) {

    if (engine == "sqlite3") {
        drogon::orm::Sqlite3Config cfg;
        cfg.connectionNumber = static_cast<size_t>(poolSize);
        cfg.filename = dbName;
        cfg.name = name;
        cfg.timeout = timeout;
        return cfg;
    } else if (engine == "mysql") {
        drogon::orm::MysqlConfig cfg;
        cfg.host = host;
        cfg.port = static_cast<unsigned short>(port);
        cfg.databaseName = dbName;
        cfg.username = user;
        cfg.password = password;
        cfg.connectionNumber = static_cast<size_t>(poolSize);
        cfg.name = name;
        cfg.isFast = fastMode;
        cfg.characterSet = "";
        cfg.timeout = timeout;
        return cfg;
    } else {
        // postgresql (default)
        drogon::orm::PostgresConfig cfg;
        cfg.host = host;
        cfg.port = static_cast<unsigned short>(port);
        cfg.databaseName = dbName;
        cfg.username = user;
        cfg.password = password;
        cfg.connectionNumber = static_cast<size_t>(poolSize);
        cfg.name = name;
        cfg.isFast = fastMode;
        cfg.characterSet = "";
        cfg.timeout = timeout;
        cfg.autoBatch = false;
        return cfg;
    }
}

} // anonymous namespace

void DatabaseManager::registerDatabases() {
    auto& config = EnvConfig::instance();

    // Estimate thread count for connection pool logging
    auto threadNum = drogon::app().getThreadNum();
    if (threadNum == 0) {
        threadNum = static_cast<size_t>(std::thread::hardware_concurrency());
    }

    // --- Primary Database (required) ---
    const auto dbRdbms = config.getDbRdbms();
    const auto dbHost = config.get("DB_HOST", "localhost");
    const auto dbPort = config.getInt("DB_PORT", config.getDbDefaultPort());
    const auto dbName = config.get("DB_NAME", "mydb");
    const auto dbUser = config.get("DB_USER", "postgres");
    const auto dbPassword = config.get("DB_PASSWORD", "");
    const auto dbPoolSize = config.getInt("DB_POOL_SIZE", 5);
    const auto dbFastMode = config.getBool("DB_FAST_MODE", true);
    const auto dbTimeout = std::stod(config.get("DB_TIMEOUT", "30.0"));

    try {
        drogon::app().addDbClient(makeDbConfig(
            dbRdbms, dbHost, dbPort, dbName, dbUser, dbPassword,
            dbPoolSize, dbFastMode, dbTimeout, "default"));
    } catch (const std::exception& e) {
        LOG_FATAL << "Failed to register database 'default': " << e.what();
        throw;
    }

    auto totalConns = (threadNum + 1) * dbPoolSize;
    LOG_INFO << "Registered database 'default': " << dbRdbms
             << "@" << dbHost << ":" << dbPort << "/" << dbName
             << " (pool: " << dbPoolSize
             << ", total: ~" << totalConns
             << ", fast: " << (dbFastMode ? "true" : "false") << ")";

    registeredNames_.push_back("default");

    // --- Analytics Database (optional) ---
    const auto analyticsHost = config.get("DB_ANALYTICS_HOST", "");
    if (!analyticsHost.empty()) {
        auto analyticsEngine = config.get("DB_ANALYTICS_ENGINE", dbRdbms);
        // Normalize engine name
        if (analyticsEngine == "postgres" || analyticsEngine == "pg")
            analyticsEngine = "postgresql";
        if (analyticsEngine == "mysql" || analyticsEngine == "mariadb")
            analyticsEngine = "mysql";
        if (analyticsEngine == "sqlite" || analyticsEngine == "sqlite3")
            analyticsEngine = "sqlite3";

        const auto analyticsPort = config.getInt("DB_ANALYTICS_PORT", 5432);
        const auto analyticsName = config.get("DB_ANALYTICS_NAME", "analytics");
        const auto analyticsUser = config.get("DB_ANALYTICS_USER", "postgres");
        const auto analyticsPassword = config.get("DB_ANALYTICS_PASSWORD", "");
        const auto analyticsPoolSize = config.getInt("DB_ANALYTICS_POOL_SIZE", 2);
        const auto analyticsFastMode = config.getBool("DB_ANALYTICS_FAST_MODE", true);
        const auto analyticsTimeout = std::stod(config.get("DB_ANALYTICS_TIMEOUT", "60.0"));

        drogon::app().addDbClient(makeDbConfig(
            analyticsEngine, analyticsHost, analyticsPort, analyticsName,
            analyticsUser, analyticsPassword, analyticsPoolSize,
            analyticsFastMode, analyticsTimeout, "analytics"));

        auto analyticsTotal = (threadNum + 1) * analyticsPoolSize;
        LOG_INFO << "Registered database 'analytics': " << analyticsEngine
                 << "@" << analyticsHost << ":" << analyticsPort << "/" << analyticsName
                 << " (pool: " << analyticsPoolSize
                 << ", total: ~" << analyticsTotal
                 << ", fast: " << (analyticsFastMode ? "true" : "false") << ")";

        registeredNames_.push_back("analytics");
    }
}

void DatabaseManager::warmPools() {
    drogon::app().registerBeginningAdvice([]() {
        for (const auto& name : registeredNames_) {
            auto client = drogon::app().getDbClient(name);
            if (!client) {
                LOG_ERROR << "Database '" << name << "' client not available for warming";
                continue;
            }
            client->execSqlAsync(
                "SELECT 1",
                [name](const drogon::orm::Result& /*result*/) {
                    LOG_INFO << "Database '" << name << "' pool warmed successfully";
                },
                [name](const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "Database '" << name << "' warming failed: "
                              << e.base().what();
                }
            );
        }
    });
}

const std::vector<std::string>& DatabaseManager::getRegisteredNames() {
    return registeredNames_;
}
