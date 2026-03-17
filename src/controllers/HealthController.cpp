// Author: Udaykiran Atta
// License: MIT

#include "controllers/HealthController.h"
#include "database/DatabaseManager.h"

#include <drogon/drogon.h>
#include <json/json.h>
#include <chrono>

drogon::Task<drogon::HttpResponsePtr> HealthController::check(drogon::HttpRequestPtr req) {
    Json::Value response;
    Json::Value databases;
    bool allHealthy = true;

    for (const auto& name : DatabaseManager::getRegisteredNames()) {
        Json::Value entry;

        try {
            auto client = drogon::app().getDbClient(name);
            if (!client) {
                entry["status"] = "not_configured";
                entry["connected"] = false;
                allHealthy = false;
                databases[name] = entry;
                continue;
            }

            bool connected = client->hasAvailableConnections();
            entry["connected"] = connected;

            if (connected) {
                auto start = std::chrono::steady_clock::now();
                co_await client->execSqlCoro("SELECT 1");
                auto end = std::chrono::steady_clock::now();

                auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start).count();

                entry["status"] = "healthy";
                entry["latency_ms"] = static_cast<Json::Int64>(latencyMs);
            } else {
                entry["status"] = "unhealthy";
                allHealthy = false;
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            entry["status"] = "error";
            entry["error"] = e.base().what();
            entry["connected"] = false;
            allHealthy = false;
        } catch (const std::exception& e) {
            entry["status"] = "error";
            entry["error"] = e.what();
            entry["connected"] = false;
            allHealthy = false;
        }

        databases[name] = entry;
    }

    response["status"] = allHealthy ? "ok" : "degraded";
    response["databases"] = databases;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(allHealthy ? drogon::k200OK : drogon::k503ServiceUnavailable);
    co_return resp;
}
