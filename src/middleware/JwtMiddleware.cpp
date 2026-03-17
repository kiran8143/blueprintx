// Author: Udaykiran Atta
// License: MIT

#include "middleware/JwtMiddleware.h"
#include "config/EnvConfig.h"

#include <drogon/drogon.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <json/json.h>

using jwt_traits = jwt::traits::open_source_parsers_jsoncpp;

// Paths that bypass JWT authentication entirely.
const std::vector<std::string> JwtFilter::skipPaths_ = {
    "/health",
    "/api/v1/auth/login",
    "/api/v1/auth/register",
};

bool JwtFilter::shouldSkipAuth(const std::string &path)
{
    for (const auto &skip : skipPaths_) {
        if (path == skip) {
            return true;
        }
    }
    return false;
}

void JwtFilter::rejectUnauthorized(drogon::FilterCallback &fcb,
                                   const std::string &message)
{
    Json::Value body;
    body["error"]["message"] = message;
    body["error"]["status"] = 401;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k401Unauthorized);
    fcb(resp);
}

void JwtFilter::doFilter(const drogon::HttpRequestPtr &req,
                          drogon::FilterCallback &&fcb,
                          drogon::FilterChainCallback &&fccb)
{
    // Allow unauthenticated access to skip-listed paths.
    if (shouldSkipAuth(req->path())) {
        fccb();
        return;
    }

    // Read the JWT secret from environment configuration.
    const auto &config = EnvConfig::instance();
    const auto secret = config.get("JWT_SECRET", "");

    if (secret.empty()) {
        LOG_ERROR << "JWT_SECRET is not configured. Rejecting request to "
                  << req->path();
        rejectUnauthorized(fcb, "Server authentication is not configured");
        return;
    }

    // Extract the Authorization header.
    const auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        LOG_WARN << "Missing Authorization header for " << req->path();
        rejectUnauthorized(fcb, "Authorization header is required");
        return;
    }

    // Expect "Bearer <token>" format.
    const std::string bearerPrefix = "Bearer ";
    if (authHeader.size() <= bearerPrefix.size() ||
        authHeader.substr(0, bearerPrefix.size()) != bearerPrefix) {
        LOG_WARN << "Malformed Authorization header for " << req->path();
        rejectUnauthorized(fcb, "Authorization header must use Bearer scheme");
        return;
    }

    const auto token = authHeader.substr(bearerPrefix.size());

    try {
        // Decode and verify the JWT token.
        auto decoded = jwt::decode<jwt_traits>(token);

        auto verifier = jwt::verify<jwt_traits>()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_issuer("drogon-blueprint");

        verifier.verify(decoded);

        // Extract claims and store in request attributes for downstream use.
        auto attrs = req->attributes();

        if (decoded.has_payload_claim("user_id")) {
            const auto &claim = decoded.get_payload_claim("user_id");
            attrs->insert("user_id", claim.as_string());
        }

        if (decoded.has_payload_claim("role")) {
            const auto &claim = decoded.get_payload_claim("role");
            attrs->insert("role", claim.as_string());
        }

        LOG_DEBUG << "JWT authenticated user for " << req->path();

        // Token is valid -- proceed to the next filter or handler.
        fccb();

    } catch (const jwt::error::token_verification_exception &e) {
        LOG_WARN << "JWT verification failed for " << req->path()
                 << ": " << e.what();
        rejectUnauthorized(fcb, "Invalid or expired token");
    } catch (const jwt::error::signature_verification_exception &e) {
        LOG_WARN << "JWT signature mismatch for " << req->path()
                 << ": " << e.what();
        rejectUnauthorized(fcb, "Invalid token signature");
    } catch (const std::exception &e) {
        LOG_ERROR << "JWT processing error for " << req->path()
                  << ": " << e.what();
        rejectUnauthorized(fcb, "Token processing failed");
    }
}
