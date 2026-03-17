// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/HttpFilter.h>
#include <string>
#include <vector>

/// JWT authentication filter for Drogon.
///
/// Extracts the Bearer token from the Authorization header, validates it
/// using jwt-cpp with the HS256 algorithm, and stores decoded claims
/// (user_id, role) in request attributes for downstream handlers.
///
/// Paths listed in the skip list (health check, login, register) bypass
/// authentication entirely.
///
/// Usage: Apply to routes via config.json filters or per-controller:
///   ADD_METHOD_TO(Controller::method, "/path", drogon::Get, "JwtFilter");
class JwtFilter : public drogon::HttpFilter<JwtFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

private:
    /// Returns true if the request path should bypass JWT authentication.
    static bool shouldSkipAuth(const std::string &path);

    /// Sends a 401 Unauthorized JSON response through the filter callback.
    static void rejectUnauthorized(drogon::FilterCallback &fcb,
                                   const std::string &message);

    /// Paths that do not require authentication.
    static const std::vector<std::string> skipPaths_;
};
