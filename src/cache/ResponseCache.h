// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "LruCache.h"

/// HTTP response caching layer built on top of LruCache.
///
/// Caches JSON response bodies keyed by "{method}:{path}:{querystring}".
/// Only GET requests are eligible for caching.
///
/// Thread-safe -- delegates all locking to the underlying LruCache.
///
/// Usage:
///   // In a GET handler, check the cache first:
///   if (auto cached = ResponseCache::instance().get(req)) {
///       co_return ApiResponse::ok(*cached);
///   }
///   // ... execute query ...
///   ResponseCache::instance().put(req, responseJson);
///
///   // After a mutation (POST/PUT/DELETE), invalidate:
///   ResponseCache::instance().invalidateTable("users");
class ResponseCache {
public:
    /// Meyer's singleton.
    static ResponseCache& instance();

    /// Look up a cached response for the given request.
    /// Returns std::nullopt for non-GET requests or cache misses.
    std::optional<Json::Value> get(const drogon::HttpRequestPtr& req);

    /// Store a response in the cache.
    /// Only caches GET requests; calls for other methods are silently ignored.
    void put(const drogon::HttpRequestPtr& req,
             const Json::Value& response,
             std::chrono::seconds ttl = std::chrono::seconds(60));

    /// Invalidate every cached response whose key contains the table name.
    /// Key format is "GET:/api/v1/{table}..." so we match on the
    /// "/api/v1/{tableName}" prefix after the method.
    void invalidateTable(const std::string& tableName);

    /// Drop all cached entries.
    void clear();

    /// Current number of cached entries.
    size_t size() const;

private:
    ResponseCache();
    ~ResponseCache() = default;
    ResponseCache(const ResponseCache&) = delete;
    ResponseCache& operator=(const ResponseCache&) = delete;

    /// Build a cache key from the request.
    /// Format: "{METHOD}:{path}:{querystring}"
    static std::string buildKey(const drogon::HttpRequestPtr& req);

    /// Default maximum number of cached entries.
    static constexpr size_t kDefaultMaxSize = 10000;

    /// Default TTL for cached responses.
    static constexpr auto kDefaultTtl = std::chrono::seconds(60);

    LruCache<std::string, Json::Value> cache_;
};
