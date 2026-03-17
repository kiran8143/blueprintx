// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/HttpFilter.h>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

/// Per-IP sliding window rate limiter implemented as a Drogon HttpFilter.
///
/// Tracks request timestamps per client IP address. When the number of
/// requests within the configured window exceeds the maximum, the filter
/// rejects the request with 429 Too Many Requests and includes a
/// Retry-After header indicating how many seconds the client should wait.
///
/// Configuration (via .env / EnvConfig):
///   RATE_LIMIT_MAX    - Maximum requests per window (default: 100)
///   RATE_LIMIT_WINDOW - Window duration in seconds  (default: 60)
///
/// Expired timestamp entries are cleaned up lazily on each request and
/// periodically via a background purge cycle.
///
/// Usage:
///   ADD_METHOD_TO(Controller::method, "/path", drogon::Get, "RateLimitFilter");
class RateLimitFilter : public drogon::HttpFilter<RateLimitFilter> {
public:
    RateLimitFilter();

    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /// Per-IP record of request timestamps within the current window.
    struct ClientRecord {
        std::deque<TimePoint> timestamps;
    };

    /// Removes timestamps older than the sliding window from a record.
    void pruneExpired(ClientRecord &record, TimePoint now) const;

    /// Removes entire client entries that have no recent activity.
    /// Called periodically to prevent unbounded memory growth.
    void purgeStaleClients();

    /// Sends a 429 Too Many Requests JSON response with Retry-After header.
    static void rejectRateLimited(drogon::FilterCallback &fcb,
                                  int retryAfterSeconds);

    /// Extracts the client IP from the request (respects X-Forwarded-For).
    static std::string getClientIp(const drogon::HttpRequestPtr &req);

    // Configuration -- loaded once at construction from EnvConfig.
    int maxRequests_;
    int windowSeconds_;
    std::chrono::seconds window_;

    // State -- guarded by mutex for thread safety across event loops.
    std::mutex mutex_;
    std::unordered_map<std::string, ClientRecord> clients_;

    // Periodic cleanup tracking.
    TimePoint lastPurge_;
    static constexpr std::chrono::seconds purgeInterval_{300}; // 5 minutes
};
