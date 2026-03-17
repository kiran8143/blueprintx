// Author: Udaykiran Atta
// License: MIT

#include "protection/FieldGuard.h"

#include <drogon/drogon.h>

// -----------------------------------------------------------------------
// Static members
// -----------------------------------------------------------------------

const std::unordered_set<std::string> FieldGuard::kDefaultBlocked = {
    "id",
    "created_at",
    "updated_at",
    "created_by",
    "modified_by",
    "deleted_at",
    "deleted_by"
};

std::unordered_map<std::string, std::unordered_set<std::string>>
    FieldGuard::tableBlocked_;

std::shared_mutex FieldGuard::mutex_;

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

Json::Value FieldGuard::sanitize(const std::string& table,
                                 const Json::Value& input) {
    if (!input.isObject()) {
        return input;
    }

    auto blocked = getBlockedFields(table);

    Json::Value result(Json::objectValue);
    for (const auto& key : input.getMemberNames()) {
        if (blocked.count(key) == 0) {
            result[key] = input[key];
        } else {
            LOG_DEBUG << "FieldGuard: stripped blocked field '"
                      << key << "' from table '" << table << "'";
        }
    }

    return result;
}

void FieldGuard::addBlockedField(const std::string& table,
                                 const std::string& field) {
    std::unique_lock lock(mutex_);
    tableBlocked_[table].insert(field);
    LOG_DEBUG << "FieldGuard: added blocked field '"
              << field << "' for table '" << table << "'";
}

void FieldGuard::setBlockedFields(const std::string& table,
                                  const std::vector<std::string>& fields) {
    std::unique_lock lock(mutex_);
    tableBlocked_[table] = std::unordered_set<std::string>(
        fields.begin(), fields.end());
    LOG_DEBUG << "FieldGuard: set " << fields.size()
              << " blocked field(s) for table '" << table << "'";
}

std::unordered_set<std::string> FieldGuard::getBlockedFields(
    const std::string& table) {
    std::shared_lock lock(mutex_);

    // Start with defaults.
    auto result = kDefaultBlocked;

    // Merge table-specific entries.
    auto it = tableBlocked_.find(table);
    if (it != tableBlocked_.end()) {
        result.insert(it->second.begin(), it->second.end());
    }

    return result;
}

void FieldGuard::resetAll() {
    std::unique_lock lock(mutex_);
    tableBlocked_.clear();
    LOG_DEBUG << "FieldGuard: reset all per-table overrides";
}
