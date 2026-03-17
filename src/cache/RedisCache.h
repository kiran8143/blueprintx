// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <optional>
#include <string>

#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <json/json.h>

/// L2 distributed cache backed by Redis.
///
/// Stores JSON values as serialized strings in Redis with configurable TTL.
/// Designed to sit behind the in-process L1 ResponseCache in a two-tier
/// caching hierarchy managed by CacheManager.
///
/// Key format: "blueprint:{table}:{method}:{path_hash}"
///
/// Graceful degradation: if Redis is not configured (REDIS_HOST empty) or
/// becomes unavailable at runtime, all operations return nullopt / no-op
/// and log a warning.  The application continues to function without L2.
///
/// Thread-safe -- delegates all concurrency to Drogon's RedisClient which
/// manages its own connection pool and event-loop dispatching.
///
/// Usage:
///   auto& redis = RedisCache::instance();
///   if (redis.isEnabled()) {
///       auto cached = co_await redis.get("blueprint:users:GET:abc123");
///       if (cached) {
///           co_return ApiResponse::ok(*cached);
///       }
///   }
class RedisCache {
public:
    /// Meyer's singleton.
    static RedisCache& instance();

    /// Look up a cached JSON value by key.
    /// Returns std::nullopt on miss, expiry, or Redis unavailability.
    drogon::Task<std::optional<Json::Value>> get(const std::string& key);

    /// Store a JSON value under the given key with TTL (seconds).
    /// Silently no-ops if Redis is unavailable.
    drogon::Task<void> put(const std::string& key,
                           const Json::Value& value,
                           int ttlSeconds = 60);

    /// Delete a single key from Redis.
    drogon::Task<void> invalidate(const std::string& key);

    /// Delete all keys matching a glob pattern using SCAN + DEL.
    /// Pattern uses Redis glob syntax (e.g. "blueprint:users:*").
    drogon::Task<void> invalidateByPattern(const std::string& pattern);

    /// Delete every cached key for a given table.
    /// Equivalent to invalidateByPattern("blueprint:{tableName}:*").
    drogon::Task<void> invalidateTable(const std::string& tableName);

    /// Returns true if Redis is configured and the client was created.
    bool isEnabled() const;

private:
    RedisCache();
    ~RedisCache() = default;
    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;

    /// Try to obtain the Drogon RedisClient.
    /// Returns nullptr if Redis is not configured or not available.
    drogon::nosql::RedisClientPtr getClient() const;

    /// Serialize a Json::Value to a compact string for storage.
    static std::string serialize(const Json::Value& value);

    /// Parse a JSON string back into a Json::Value.
    /// Returns std::nullopt if parsing fails.
    static std::optional<Json::Value> deserialize(const std::string& str);

    /// Key prefix used by all cache entries.
    static constexpr const char* kKeyPrefix = "blueprint:";

    /// Whether Redis was configured at startup (REDIS_HOST non-empty).
    bool enabled_ = false;

    /// Whether a Redis client was actually registered with Drogon.
    /// Set by markRegistered(). Without this, getRedisClient() would
    /// trigger a fatal assertion in Drogon.
    bool clientRegistered_ = false;

public:
    /// Call after successfully registering a Redis client with Drogon.
    /// Without this, RedisCache will not attempt to call getRedisClient().
    void markRegistered() { clientRegistered_ = true; }
};
