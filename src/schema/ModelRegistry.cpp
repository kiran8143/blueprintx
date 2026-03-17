// Author: Udaykiran Atta
// License: MIT

#include "schema/ModelRegistry.h"
#include <algorithm>
#include <mutex>

ModelRegistry& ModelRegistry::instance() {
    static ModelRegistry registry;
    return registry;
}

void ModelRegistry::registerTable(const std::string& tableName, TableMeta meta) {
    std::unique_lock lock(mutex_);
    tables_[tableName] = std::move(meta);
}

const TableMeta* ModelRegistry::getTable(const std::string& tableName) const {
    std::shared_lock lock(mutex_);
    auto it = tables_.find(tableName);
    if (it != tables_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> ModelRegistry::getTableNames() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [name, _] : tables_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

size_t ModelRegistry::tableCount() const {
    std::shared_lock lock(mutex_);
    return tables_.size();
}

size_t ModelRegistry::totalColumnCount() const {
    std::shared_lock lock(mutex_);
    size_t total = 0;
    for (const auto& [_, meta] : tables_) {
        total += meta.columns.size();
    }
    return total;
}

void ModelRegistry::clear() {
    std::unique_lock lock(mutex_);
    tables_.clear();
}
