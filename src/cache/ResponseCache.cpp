// Author: Udaykiran Atta
// License: MIT

#include "cache/ResponseCache.h"

ResponseCache& ResponseCache::instance() {
    static ResponseCache inst;
    return inst;
}

ResponseCache::ResponseCache()
    : cache_(kDefaultMaxSize, kDefaultTtl) {}

std::optional<Json::Value> ResponseCache::get(
    const drogon::HttpRequestPtr& req) {
    // Only cache GET requests
    if (req->getMethod() != drogon::Get) {
        return std::nullopt;
    }

    return cache_.get(buildKey(req));
}

void ResponseCache::put(const drogon::HttpRequestPtr& req,
                         const Json::Value& response,
                         std::chrono::seconds ttl) {
    // Only cache GET requests
    if (req->getMethod() != drogon::Get) {
        return;
    }

    cache_.put(buildKey(req), response, ttl);
}

void ResponseCache::invalidateTable(const std::string& tableName) {
    // Cache keys for table endpoints look like:
    //   "GET:/api/v1/{tableName}..."
    // We match any key that starts with this prefix.
    const std::string prefix = "GET:/api/v1/" + tableName;
    cache_.invalidateByPrefix(prefix);
}

void ResponseCache::clear() {
    cache_.clear();
}

size_t ResponseCache::size() const {
    return cache_.size();
}

std::string ResponseCache::buildKey(const drogon::HttpRequestPtr& req) {
    // Method string
    std::string method;
    switch (req->getMethod()) {
        case drogon::Get:     method = "GET";     break;
        case drogon::Post:    method = "POST";    break;
        case drogon::Put:     method = "PUT";     break;
        case drogon::Delete:  method = "DELETE";   break;
        case drogon::Patch:   method = "PATCH";   break;
        default:              method = "OTHER";   break;
    }

    // Path (without query string)
    std::string path(req->path());

    // Query string
    std::string query(req->query());

    // Format: "{METHOD}:{path}:{querystring}"
    std::string key;
    key.reserve(method.size() + 1 + path.size() + 1 + query.size());
    key += method;
    key += ':';
    key += path;
    key += ':';
    key += query;
    return key;
}
