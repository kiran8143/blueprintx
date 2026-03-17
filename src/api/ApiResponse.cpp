// Author: Udaykiran Atta
// License: MIT

#include "api/ApiResponse.h"

using namespace drogon;

HttpResponsePtr ApiResponse::ok(const Json::Value& data) {
    auto resp = HttpResponse::newHttpJsonResponse(data);
    resp->setStatusCode(k200OK);
    return resp;
}

HttpResponsePtr ApiResponse::created(const Json::Value& data) {
    auto resp = HttpResponse::newHttpJsonResponse(data);
    resp->setStatusCode(k201Created);
    return resp;
}

HttpResponsePtr ApiResponse::noContent() {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    return resp;
}

HttpResponsePtr ApiResponse::paginated(
    const Json::Value& data,
    size_t total,
    size_t limit,
    size_t offset) {
    Json::Value body;
    body["data"] = data;

    Json::Value meta;
    meta["total"] = static_cast<Json::UInt64>(total);
    meta["limit"] = static_cast<Json::UInt64>(limit);
    meta["offset"] = static_cast<Json::UInt64>(offset);
    meta["has_more"] = (offset + limit) < total;
    body["meta"] = meta;

    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(k200OK);
    return resp;
}

HttpResponsePtr ApiResponse::error(
    HttpStatusCode status,
    const std::string& message,
    const Json::Value& details) {
    Json::Value body;
    body["error"]["message"] = message;
    body["error"]["status"] = static_cast<int>(status);
    if (!details.isNull()) {
        body["error"]["details"] = details;
    }

    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

HttpResponsePtr ApiResponse::badRequest(
    const std::string& message,
    const Json::Value& details) {
    return error(k400BadRequest, message, details);
}

HttpResponsePtr ApiResponse::notFound(const std::string& message) {
    return error(k404NotFound, message);
}

HttpResponsePtr ApiResponse::conflict(const std::string& message) {
    return error(k409Conflict, message);
}

HttpResponsePtr ApiResponse::internalError(const std::string& message) {
    return error(k500InternalServerError, message);
}

HttpResponsePtr ApiResponse::validationError(const Json::Value& errors) {
    return error(k422UnprocessableEntity, "Validation failed", errors);
}
