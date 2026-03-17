// Author: Udaykiran Atta
// License: MIT

#include "cache/CacheManager.h"
#include "cache/RedisCache.h"
#include "cache/ResponseCache.h"

#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>

CacheManager& CacheManager::instance() {
    static CacheManager inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Tiered lookup: L1 (in-process) -> L2 (Redis)
// ---------------------------------------------------------------------------

drogon::Task<std::optional<Json::Value>> CacheManager::get(
    const drogon::HttpRequestPtr& req) {
    // Only GET requests are cacheable
    if (req->getMethod() != drogon::Get) {
        co_return std::nullopt;
    }

    // L1: in-process LRU (synchronous, no co_await needed)
    auto l1Hit = ResponseCache::instance().get(req);
    if (l1Hit) {
        LOG_TRACE << "CacheManager: L1 HIT";
        co_return l1Hit;
    }

    // L2: Redis (async, needs co_await)
    auto& redis = RedisCache::instance();
    if (redis.isEnabled()) {
        auto redisKey = buildCacheKey(req);
        auto l2Hit = co_await redis.get(redisKey);
        if (l2Hit) {
            LOG_TRACE << "CacheManager: L2 HIT, back-filling L1";
            // Back-fill L1 so subsequent requests skip Redis
            ResponseCache::instance().put(req, *l2Hit);
            co_return l2Hit;
        }
    }

    LOG_TRACE << "CacheManager: MISS (L1 + L2)";
    co_return std::nullopt;
}

// ---------------------------------------------------------------------------
// Write-through to both tiers
// ---------------------------------------------------------------------------

drogon::Task<void> CacheManager::put(const drogon::HttpRequestPtr& req,
                                      const Json::Value& data,
                                      int ttlSeconds) {
    // Only cache GET requests
    if (req->getMethod() != drogon::Get) {
        co_return;
    }

    // L1: synchronous put
    ResponseCache::instance().put(
        req, data, std::chrono::seconds(ttlSeconds));

    // L2: async put
    auto& redis = RedisCache::instance();
    if (redis.isEnabled()) {
        auto redisKey = buildCacheKey(req);
        co_await redis.put(redisKey, data, ttlSeconds);
    }
}

// ---------------------------------------------------------------------------
// Invalidation across both tiers
// ---------------------------------------------------------------------------

drogon::Task<void> CacheManager::invalidateTable(
    const std::string& tableName) {
    // L1: synchronous prefix-based invalidation
    ResponseCache::instance().invalidateTable(tableName);

    // L2: async pattern-based invalidation
    auto& redis = RedisCache::instance();
    if (redis.isEnabled()) {
        co_await redis.invalidateTable(tableName);
    }
}

// ---------------------------------------------------------------------------
// Key building
// ---------------------------------------------------------------------------

std::string CacheManager::buildCacheKey(const drogon::HttpRequestPtr& req) {
    std::string path(req->path());
    std::string query(req->query());
    std::string method = methodString(req->getMethod());
    std::string table = extractTableName(path);

    if (table.empty()) {
        table = "unknown";
    }

    // Hash the full path + query for uniqueness
    std::string hashInput = path + "?" + query;
    std::string pathHash = hashString(hashInput);

    // Format: "blueprint:{table}:{METHOD}:{hash}"
    return "blueprint:" + table + ":" + method + ":" + pathHash;
}

std::string CacheManager::extractTableName(const std::string& path) {
    // Expected format: "/api/v1/{tableName}" or "/api/v1/{tableName}/{id}"
    const std::string prefix = "/api/v1/";
    if (path.size() <= prefix.size() ||
        path.substr(0, prefix.size()) != prefix) {
        return "";
    }

    auto remainder = path.substr(prefix.size());
    auto slashPos = remainder.find('/');
    if (slashPos != std::string::npos) {
        return remainder.substr(0, slashPos);
    }
    return remainder;
}

std::string CacheManager::hashString(const std::string& input) {
    auto hash = std::hash<std::string>{}(input);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string CacheManager::methodString(drogon::HttpMethod method) {
    switch (method) {
        case drogon::Get:    return "GET";
        case drogon::Post:   return "POST";
        case drogon::Put:    return "PUT";
        case drogon::Delete: return "DELETE";
        case drogon::Patch:  return "PATCH";
        default:             return "OTHER";
    }
}
