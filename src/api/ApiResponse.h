// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

class ApiResponse {
public:
    // Success responses
    static drogon::HttpResponsePtr ok(const Json::Value& data);
    static drogon::HttpResponsePtr created(const Json::Value& data);
    static drogon::HttpResponsePtr noContent();

    // Paginated response with metadata
    static drogon::HttpResponsePtr paginated(
        const Json::Value& data,
        size_t total,
        size_t limit,
        size_t offset);

    // Error responses
    static drogon::HttpResponsePtr error(
        drogon::HttpStatusCode status,
        const std::string& message,
        const Json::Value& details = Json::nullValue);

    static drogon::HttpResponsePtr badRequest(
        const std::string& message,
        const Json::Value& details = Json::nullValue);

    static drogon::HttpResponsePtr notFound(const std::string& message);
    static drogon::HttpResponsePtr conflict(const std::string& message);
    static drogon::HttpResponsePtr internalError(const std::string& message);
    static drogon::HttpResponsePtr validationError(const Json::Value& errors);
};
