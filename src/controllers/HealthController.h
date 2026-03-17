// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/HttpController.h>

class HealthController : public drogon::HttpController<HealthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::check, "/health", drogon::Get);
    METHOD_LIST_END

    /// Health check handler - returns per-database status with latency.
    /// Returns 200 if all databases healthy, 503 if any degraded/unreachable.
    drogon::Task<drogon::HttpResponsePtr> check(drogon::HttpRequestPtr req);
};
