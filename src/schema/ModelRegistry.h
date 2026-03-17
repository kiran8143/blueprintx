// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include "Types.h"

class ModelRegistry {
public:
    static ModelRegistry& instance();

    void registerTable(const std::string& tableName, TableMeta meta);
    const TableMeta* getTable(const std::string& tableName) const;
    std::vector<std::string> getTableNames() const;
    size_t tableCount() const;
    size_t totalColumnCount() const;
    void clear();

    ModelRegistry(const ModelRegistry&) = delete;
    ModelRegistry& operator=(const ModelRegistry&) = delete;

private:
    ModelRegistry() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, TableMeta> tables_;
};
