// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/HttpFilter.h>
#include <vector>
#include <string>

class CorsFilter : public drogon::HttpFilter<CorsFilter> {
public:
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;

private:
    static std::vector<std::string> getAllowedOrigins();
    static bool isOriginAllowed(const std::string& origin,
                                 const std::vector<std::string>& allowed);
};
