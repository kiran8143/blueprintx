// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <algorithm>

enum class SqlType {
    Integer,
    Float,
    Decimal,
    String,
    Boolean,
    DateTime,
    Date,
    Time,
    Binary,
    Json,
    Uuid,
    Unknown
};

enum class JsonType {
    Number,
    String,
    Boolean,
    Null,
    Object,
    Array
};

struct ColumnMeta {
    std::string name;
    std::string rawType;
    SqlType sqlType = SqlType::Unknown;
    JsonType jsonType = JsonType::String;
    bool isNullable = true;
    bool isPrimaryKey = false;
    bool isAutoIncrement = false;
    std::optional<std::string> defaultValue;
    std::optional<int> maxLength;
    std::optional<int> precision;
    std::optional<int> scale;
    int ordinalPosition = 0;
};

struct ForeignKeyMeta {
    std::string columnName;
    std::string referencedTable;
    std::string referencedColumn;
    std::string constraintName;
};

struct TableMeta {
    std::string name;
    std::string schema;
    std::vector<ColumnMeta> columns;
    std::vector<std::string> primaryKeys;
    std::vector<ForeignKeyMeta> foreignKeys;

    const ColumnMeta* getColumn(const std::string& colName) const {
        for (const auto& col : columns) {
            if (col.name == colName) return &col;
        }
        return nullptr;
    }

    bool hasColumn(const std::string& colName) const {
        return getColumn(colName) != nullptr;
    }

    bool isGenericField(const std::string& colName) const {
        static const std::vector<std::string> genericFields = {
            "id", "code", "created_at", "updated_at",
            "created_by", "modified_by", "deleted_at", "deleted_by", "status"
        };
        return std::find(genericFields.begin(), genericFields.end(), colName) != genericFields.end();
    }
};
