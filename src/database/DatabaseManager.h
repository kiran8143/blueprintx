// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <string>
#include <vector>

/// Static utility class for managing database lifecycle.
/// Handles multi-database registration, connection pool warming,
/// and tracking registered database names for health checks.
class DatabaseManager {
public:
    /// Register all configured databases from EnvConfig.
    /// MUST be called BEFORE drogon::app().run().
    /// Reads DB_* for primary, DB_ANALYTICS_* for analytics (optional).
    static void registerDatabases();

    /// Register pool warming callbacks via registerBeginningAdvice().
    /// MUST be called BEFORE drogon::app().run().
    /// Warming runs AFTER event loops start, BEFORE accepting requests.
    static void warmPools();

    /// Get list of registered database client names (e.g., {"default", "analytics"}).
    /// Used by HealthController to enumerate databases for health checks.
    static const std::vector<std::string>& getRegisteredNames();

private:
    DatabaseManager() = delete;
    static std::vector<std::string> registeredNames_;
};
