// Author: Udaykiran Atta
// License: MIT

#include "middleware/RateLimiter.h"
#include "config/EnvConfig.h"

#include <drogon/drogon.h>
#include <json/json.h>

RateLimitFilter::RateLimitFilter()
    : maxRequests_(EnvConfig::instance().getInt("RATE_LIMIT_MAX", 100)),
      windowSeconds_(EnvConfig::instance().getInt("RATE_LIMIT_WINDOW", 60)),
      window_(std::chrono::seconds(windowSeconds_)),
      lastPurge_(Clock::now())
{
    LOG_INFO << "RateLimitFilter initialized: max=" << maxRequests_
             << " requests per " << windowSeconds_ << "s window";
}

void RateLimitFilter::pruneExpired(ClientRecord &record, TimePoint now) const
{
    const auto cutoff = now - window_;
    while (!record.timestamps.empty() && record.timestamps.front() < cutoff) {
        record.timestamps.pop_front();
    }
}

void RateLimitFilter::purgeStaleClients()
{
    // Caller must already hold mutex_.
    const auto now = Clock::now();
    if (now - lastPurge_ < purgeInterval_) {
        return;
    }

    lastPurge_ = now;
    size_t purged = 0;

    for (auto it = clients_.begin(); it != clients_.end(); /* no increment */) {
        pruneExpired(it->second, now);
        if (it->second.timestamps.empty()) {
            it = clients_.erase(it);
            ++purged;
        } else {
            ++it;
        }
    }

    if (purged > 0) {
        LOG_DEBUG << "RateLimitFilter purged " << purged
                  << " stale client entries, " << clients_.size()
                  << " remaining";
    }
}

void RateLimitFilter::rejectRateLimited(drogon::FilterCallback &fcb,
                                        int retryAfterSeconds)
{
    Json::Value body;
    body["error"]["message"] = "Too many requests. Please try again later.";
    body["error"]["status"] = 429;
    body["error"]["retry_after"] = retryAfterSeconds;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k429TooManyRequests);
    resp->addHeader("Retry-After", std::to_string(retryAfterSeconds));
    fcb(resp);
}

std::string RateLimitFilter::getClientIp(const drogon::HttpRequestPtr &req)
{
    // Prefer X-Forwarded-For when behind a reverse proxy.
    const auto forwarded = req->getHeader("X-Forwarded-For");
    if (!forwarded.empty()) {
        // X-Forwarded-For may contain multiple IPs: "client, proxy1, proxy2".
        // The first entry is the original client IP.
        auto commaPos = forwarded.find(',');
        if (commaPos != std::string::npos) {
            auto ip = forwarded.substr(0, commaPos);
            // Trim whitespace.
            auto start = ip.find_first_not_of(" \t");
            auto end = ip.find_last_not_of(" \t");
            if (start != std::string::npos) {
                return ip.substr(start, end - start + 1);
            }
        }
        return forwarded;
    }

    // Fall back to the direct peer address.
    return req->peerAddr().toIp();
}

void RateLimitFilter::doFilter(const drogon::HttpRequestPtr &req,
                                drogon::FilterCallback &&fcb,
                                drogon::FilterChainCallback &&fccb)
{
    const auto clientIp = getClientIp(req);
    const auto now = Clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    // Periodically clean up entries for clients that have gone silent.
    purgeStaleClients();

    auto &record = clients_[clientIp];

    // Remove timestamps outside the sliding window.
    pruneExpired(record, now);

    if (static_cast<int>(record.timestamps.size()) >= maxRequests_) {
        // Calculate how long until the oldest request in the window expires.
        const auto oldest = record.timestamps.front();
        const auto unblockTime = oldest + window_;
        auto retryAfter = std::chrono::duration_cast<std::chrono::seconds>(
            unblockTime - now).count();

        // Ensure at least 1 second is reported.
        if (retryAfter <= 0) {
            retryAfter = 1;
        }

        LOG_WARN << "Rate limit exceeded for " << clientIp
                 << " on " << req->path()
                 << " (" << record.timestamps.size()
                 << "/" << maxRequests_ << " in " << windowSeconds_ << "s)";

        rejectRateLimited(fcb, static_cast<int>(retryAfter));
        return;
    }

    // Record this request and proceed.
    record.timestamps.push_back(now);
    fccb();
}
