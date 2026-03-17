// Author: Udaykiran Atta
// License: MIT

#include "cache/RedisCache.h"
#include "config/EnvConfig.h"

#include <sstream>

RedisCache& RedisCache::instance() {
    static RedisCache inst;
    return inst;
}

RedisCache::RedisCache() {
    auto& config = EnvConfig::instance();
    const auto host = config.get("REDIS_HOST", "");
    if (!host.empty()) {
        enabled_ = true;
        LOG_INFO << "RedisCache: L2 cache enabled (host=" << host
                 << ", port=" << config.getInt("REDIS_PORT", 6379) << ")";
    } else {
        LOG_WARN << "RedisCache: REDIS_HOST not set, L2 cache disabled";
    }
}

bool RedisCache::isEnabled() const {
    return enabled_;
}

drogon::nosql::RedisClientPtr RedisCache::getClient() const {
    if (!enabled_ || !clientRegistered_) {
        return nullptr;
    }
    try {
        return drogon::app().getRedisClient("default");
    } catch (const std::exception& e) {
        LOG_WARN << "RedisCache: failed to get Redis client: " << e.what();
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Core operations
// ---------------------------------------------------------------------------

drogon::Task<std::optional<Json::Value>> RedisCache::get(
    const std::string& key) {
    auto client = getClient();
    if (!client) {
        co_return std::nullopt;
    }

    try {
        auto result = co_await client->execCommandCoro("GET %s", key.c_str());
        if (result.isNil()) {
            co_return std::nullopt;
        }
        auto parsed = deserialize(result.asString());
        if (parsed) {
            LOG_TRACE << "RedisCache: HIT " << key;
        }
        co_return parsed;
    } catch (const std::exception& e) {
        LOG_WARN << "RedisCache: GET failed for key '" << key
                 << "': " << e.what();
        co_return std::nullopt;
    }
}

drogon::Task<void> RedisCache::put(const std::string& key,
                                    const Json::Value& value,
                                    int ttlSeconds) {
    auto client = getClient();
    if (!client) {
        co_return;
    }

    try {
        auto serialized = serialize(value);
        co_await client->execCommandCoro(
            "SET %s %s EX %d", key.c_str(), serialized.c_str(), ttlSeconds);
        LOG_TRACE << "RedisCache: SET " << key << " (TTL=" << ttlSeconds << "s)";
    } catch (const std::exception& e) {
        LOG_WARN << "RedisCache: SET failed for key '" << key
                 << "': " << e.what();
    }
}

drogon::Task<void> RedisCache::invalidate(const std::string& key) {
    auto client = getClient();
    if (!client) {
        co_return;
    }

    try {
        co_await client->execCommandCoro("DEL %s", key.c_str());
        LOG_TRACE << "RedisCache: DEL " << key;
    } catch (const std::exception& e) {
        LOG_WARN << "RedisCache: DEL failed for key '" << key
                 << "': " << e.what();
    }
}

drogon::Task<void> RedisCache::invalidateByPattern(
    const std::string& pattern) {
    auto client = getClient();
    if (!client) {
        co_return;
    }

    try {
        // Use SCAN to iterate keys matching the pattern.
        // SCAN is non-blocking (unlike KEYS) and safe for production.
        std::string cursor = "0";
        size_t deleted = 0;

        do {
            auto result = co_await client->execCommandCoro(
                "SCAN %s MATCH %s COUNT 100",
                cursor.c_str(), pattern.c_str());

            // SCAN returns an array: [cursor, [key1, key2, ...]]
            // result is a Redis array; index 0 = new cursor, index 1 = keys
            auto arr = result.asArray();
            auto newCursor = arr[0].asString();
            const auto& keys = arr[1].asArray();

            for (const auto& keyResult : keys) {
                auto keyStr = keyResult.asString();
                co_await client->execCommandCoro("DEL %s", keyStr.c_str());
                ++deleted;
            }

            cursor = newCursor;
        } while (cursor != "0");

        if (deleted > 0) {
            LOG_DEBUG << "RedisCache: invalidated " << deleted
                      << " keys matching '" << pattern << "'";
        }
    } catch (const std::exception& e) {
        LOG_WARN << "RedisCache: pattern invalidation failed for '"
                 << pattern << "': " << e.what();
    }
}

drogon::Task<void> RedisCache::invalidateTable(const std::string& tableName) {
    co_await invalidateByPattern(
        std::string(kKeyPrefix) + tableName + ":*");
}

// ---------------------------------------------------------------------------
// JSON serialization helpers
// ---------------------------------------------------------------------------

std::string RedisCache::serialize(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";  // compact (no whitespace)
    return Json::writeString(builder, value);
}

std::optional<Json::Value> RedisCache::deserialize(const std::string& str) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;

    std::istringstream stream(str);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        LOG_WARN << "RedisCache: JSON parse failed: " << errors;
        return std::nullopt;
    }
    return root;
}
