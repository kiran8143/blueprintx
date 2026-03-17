// Author: Udaykiran Atta
// License: MIT

#include "middleware/CorsMiddleware.h"
#include "config/EnvConfig.h"

void CorsFilter::doFilter(const drogon::HttpRequestPtr& req,
                           drogon::FilterCallback&& fcb,
                           drogon::FilterChainCallback&& fccb) {
    auto origin = req->getHeader("Origin");
    auto allowedOrigins = getAllowedOrigins();

    if (origin.empty() || isOriginAllowed(origin, allowedOrigins)) {
        // Handle preflight OPTIONS requests
        if (req->method() == drogon::Options) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k204NoContent);

            if (!origin.empty()) {
                resp->addHeader("Access-Control-Allow-Origin", origin);
                resp->addHeader("Access-Control-Allow-Methods",
                    "GET, POST, PUT, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, Authorization, X-Requested-With");
                resp->addHeader("Access-Control-Max-Age", "86400");
                resp->addHeader("Access-Control-Allow-Credentials", "true");
            }
            fcb(resp);
            return;
        }

        // For regular requests, add CORS headers via post-routing advice
        if (!origin.empty()) {
            req->attributes()->insert("cors_origin", origin);
        }
        fccb();
        return;
    }

    // Origin not allowed
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k403Forbidden);
    fcb(resp);
}

std::vector<std::string> CorsFilter::getAllowedOrigins() {
    auto& config = EnvConfig::instance();
    auto origins = config.getList("CORS_ORIGINS");
    if (origins.empty()) {
        // Default: allow all in development
        auto env = config.get("ENVIRONMENT", "development");
        if (env == "development") {
            return {"*"};
        }
    }
    return origins;
}

bool CorsFilter::isOriginAllowed(const std::string& origin,
                                  const std::vector<std::string>& allowed) {
    for (const auto& a : allowed) {
        if (a == "*" || a == origin) return true;
    }
    return false;
}
