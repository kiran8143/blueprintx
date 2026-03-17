// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

/// Mass-assignment protection.
///
/// Strips dangerous fields (primary keys, audit columns, soft-delete markers)
/// from incoming JSON payloads before they reach the query layer.
/// Thread-safe -- reads use a shared lock, writes use an exclusive lock.
class FieldGuard {
public:
    /// Remove every blocked field from `input` for the given table.
    /// Returns a new Json::Value with blocked fields stripped out.
    static Json::Value sanitize(const std::string& table,
                                const Json::Value& input);

    /// Add a single field to the blocklist for a specific table.
    static void addBlockedField(const std::string& table,
                                const std::string& field);

    /// Replace the entire blocklist for a specific table.
    static void setBlockedFields(const std::string& table,
                                 const std::vector<std::string>& fields);

    /// Return the current blocklist for a table (defaults + table-specific).
    static std::unordered_set<std::string> getBlockedFields(
        const std::string& table);

    /// Reset all per-table overrides (reverts to defaults only).
    static void resetAll();

private:
    /// Default fields that are always blocked unless explicitly overridden.
    static const std::unordered_set<std::string> kDefaultBlocked;

    /// Per-table additional blocked fields.
    static std::unordered_map<std::string, std::unordered_set<std::string>>
        tableBlocked_;

    static std::shared_mutex mutex_;
};
