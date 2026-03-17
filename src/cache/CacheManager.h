// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <optional>
#include <string>

#include <drogon/drogon.h>
#include <json/json.h>

/// Unified cache manager implementing a two-tier L1 + L2 cache hierarchy.
///
/// Lookup order:
///   1. L1 (ResponseCache) -- in-process LRU, microsecond latency
///   2. L2 (RedisCache)    -- distributed Redis, sub-millisecond latency
///
/// On a cache miss at both levels, the caller executes the query and then
/// calls put() which writes to both L1 and L2 simultaneously.
///
/// Invalidation propagates to both tiers to prevent stale data.
///
/// Usage:
///   auto& cm = CacheManager::instance();
///   if (auto cached = co_await cm.get(req)) {
///       co_return ApiResponse::ok(*cached);
///   }
///   // ... execute query ...
///   co_await cm.put(req, result, 120);
///
///   // After mutation:
///   co_await cm.invalidateTable("users");
class CacheManager {
public:
    /// Meyer's singleton.
    static CacheManager& instance();

    /// Look up a cached response for the given request.
    /// Checks L1 first, then L2.  On L2 hit, back-fills L1.
    /// Returns std::nullopt on complete miss.
    drogon::Task<std::optional<Json::Value>> get(
        const drogon::HttpRequestPtr& req);

    /// Store a response in both L1 and L2 caches.
    /// Only caches GET requests; other methods are silently ignored.
    drogon::Task<void> put(const drogon::HttpRequestPtr& req,
                           const Json::Value& data,
                           int ttlSeconds = 60);

    /// Invalidate all cached entries for a table in both L1 and L2.
    drogon::Task<void> invalidateTable(const std::string& tableName);

    /// Build a cache key from the request for use with L2 (Redis).
    /// Format: "blueprint:{table}:{METHOD}:{hash_of_path_and_query}"
    /// Falls back to "blueprint:unknown:{METHOD}:{hash}" if the table
    /// name cannot be extracted from the path.
    static std::string buildCacheKey(const drogon::HttpRequestPtr& req);

private:
    CacheManager() = default;
    ~CacheManager() = default;
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    /// Extract the table name from an API path like "/api/v1/{table}/...".
    /// Returns empty string if the path does not match the expected format.
    static std::string extractTableName(const std::string& path);

    /// Simple hash of a string to produce a compact key component.
    /// Uses std::hash<std::string> and formats as hex.
    static std::string hashString(const std::string& input);

    /// Convert HTTP method enum to string.
    static std::string methodString(drogon::HttpMethod method);
};
