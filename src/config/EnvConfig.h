// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <string>
#include <unordered_map>
#include <cstdlib>
#include <vector>

/// Singleton configuration manager that loads .env files and provides
/// typed access to environment variables with defaults.
///
/// Usage:
///   EnvConfig::instance().load(".env");
///   auto port = EnvConfig::instance().getInt("PORT", 8080);
///   auto dbEngine = EnvConfig::instance().get("DB_ENGINE", "postgresql");
class EnvConfig {
public:
    static EnvConfig& instance();

    /// Load a .env file. Parses KEY=VALUE lines, skips comments (#) and blanks.
    /// Sets values as environment variables via setenv().
    /// Does NOT override existing environment variables (system env takes precedence).
    void load(const std::string& filepath = ".env");

    /// Get string value, returns defaultValue if not set
    std::string get(const std::string& key, const std::string& defaultValue = "") const;

    /// Get integer value, returns defaultValue if not set or not parseable
    int getInt(const std::string& key, int defaultValue = 0) const;

    /// Get boolean value (true/false/1/0/yes/no), returns defaultValue if not set
    bool getBool(const std::string& key, bool defaultValue = false) const;

    /// Get comma-separated list as vector of strings
    std::vector<std::string> getList(const std::string& key, const std::string& delimiter = ",") const;

    /// Check if a key exists in environment
    bool has(const std::string& key) const;

    /// Get the Drogon-compatible RDBMS string from DB_ENGINE
    /// Maps: postgresql -> postgresql, mysql -> mysql, sqlite3 -> sqlite3
    std::string getDbRdbms() const;

    /// Get the default port for the configured DB engine
    int getDbDefaultPort() const;

private:
    EnvConfig() = default;
    EnvConfig(const EnvConfig&) = delete;
    EnvConfig& operator=(const EnvConfig&) = delete;

    bool loaded_ = false;
};
