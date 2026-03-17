// Author: Udaykiran Atta
// License: MIT

#include "config/EnvConfig.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

EnvConfig& EnvConfig::instance() {
    static EnvConfig inst;
    return inst;
}

void EnvConfig::load(const std::string& filepath) {
    if (loaded_) return;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[EnvConfig] Warning: Could not open " << filepath
                  << ", using system environment variables only." << std::endl;
        loaded_ = true;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading/trailing whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // blank line
        line = line.substr(start);
        auto end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        // Skip comments
        if (line[0] == '#') continue;

        // Find KEY=VALUE separator
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;  // malformed line

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // Trim key and value
        auto keyEnd = key.find_last_not_of(" \t");
        if (keyEnd != std::string::npos) key = key.substr(0, keyEnd + 1);

        auto valStart = value.find_first_not_of(" \t");
        if (valStart != std::string::npos) {
            value = value.substr(valStart);
        } else {
            value = "";
        }

        // Strip surrounding quotes from value (single or double)
        if (value.size() >= 2) {
            if ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }

        // Strip inline comments (only if preceded by whitespace)
        auto commentPos = value.find(" #");
        if (commentPos != std::string::npos) {
            value = value.substr(0, commentPos);
            auto vEnd = value.find_last_not_of(" \t");
            if (vEnd != std::string::npos) {
                value = value.substr(0, vEnd + 1);
            }
        }

        // Set environment variable (do NOT override existing - system env wins)
        setenv(key.c_str(), value.c_str(), 0);
    }

    loaded_ = true;
}

std::string EnvConfig::get(const std::string& key, const std::string& defaultValue) const {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : defaultValue;
}

int EnvConfig::getInt(const std::string& key, int defaultValue) const {
    const char* val = std::getenv(key.c_str());
    if (!val) return defaultValue;
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultValue;
    }
}

bool EnvConfig::getBool(const std::string& key, bool defaultValue) const {
    const char* val = std::getenv(key.c_str());
    if (!val) return defaultValue;
    std::string s(val);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "true" || s == "1" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "no") return false;
    return defaultValue;
}

std::vector<std::string> EnvConfig::getList(const std::string& key, const std::string& delimiter) const {
    std::vector<std::string> result;
    std::string val = get(key);
    if (val.empty()) return result;

    std::string::size_type start = 0;
    std::string::size_type end;
    while ((end = val.find(delimiter, start)) != std::string::npos) {
        std::string item = val.substr(start, end - start);
        auto s = item.find_first_not_of(" \t");
        auto e = item.find_last_not_of(" \t");
        if (s != std::string::npos) {
            result.push_back(item.substr(s, e - s + 1));
        }
        start = end + delimiter.size();
    }
    // Last element
    std::string item = val.substr(start);
    auto s = item.find_first_not_of(" \t");
    auto e = item.find_last_not_of(" \t");
    if (s != std::string::npos) {
        result.push_back(item.substr(s, e - s + 1));
    }
    return result;
}

bool EnvConfig::has(const std::string& key) const {
    return std::getenv(key.c_str()) != nullptr;
}

std::string EnvConfig::getDbRdbms() const {
    std::string engine = get("DB_ENGINE", "postgresql");
    // Normalize to Drogon's expected values
    if (engine == "postgres" || engine == "pg") return "postgresql";
    if (engine == "mysql" || engine == "mariadb") return "mysql";
    if (engine == "sqlite" || engine == "sqlite3") return "sqlite3";
    return engine;  // pass through (postgresql, mysql, sqlite3)
}

int EnvConfig::getDbDefaultPort() const {
    std::string rdbms = getDbRdbms();
    if (rdbms == "postgresql") return 5432;
    if (rdbms == "mysql") return 3306;
    return 0;  // sqlite3 has no port
}
