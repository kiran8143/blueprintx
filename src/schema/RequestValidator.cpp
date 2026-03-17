// Author: Udaykiran Atta
// License: MIT

#include "schema/RequestValidator.h"

#include <algorithm>

std::vector<ValidationError> RequestValidator::validateCreate(
    const Json::Value& body,
    const TableMeta& meta,
    const std::vector<std::string>& autoFields) {
    std::vector<ValidationError> errors;

    for (const auto& col : meta.columns) {
        if (col.isAutoIncrement) continue;

        if (std::find(autoFields.begin(), autoFields.end(), col.name) != autoFields.end())
            continue;

        bool present = body.isMember(col.name);
        bool isNull = present && body[col.name].isNull();

        if (!col.isNullable && !col.defaultValue.has_value() && (!present || isNull)) {
            errors.push_back({col.name, "REQUIRED",
                "Field '" + col.name + "' is required"});
            continue;
        }

        if (!present || isNull) continue;

        if (!isTypeCompatible(body[col.name], col)) {
            errors.push_back({col.name, "INVALID_TYPE",
                "Field '" + col.name + "' expects type " + col.rawType});
        }

        if (exceedsMaxLength(body[col.name], col)) {
            errors.push_back({col.name, "TOO_LONG",
                "Field '" + col.name + "' exceeds max length " +
                std::to_string(*col.maxLength)});
        }
    }

    for (const auto& key : body.getMemberNames()) {
        if (!meta.hasColumn(key)) {
            errors.push_back({key, "UNKNOWN_FIELD",
                "Field '" + key + "' does not exist in table '" + meta.name + "'"});
        }
    }

    return errors;
}

std::vector<ValidationError> RequestValidator::validateUpdate(
    const Json::Value& body,
    const TableMeta& meta) {
    std::vector<ValidationError> errors;

    for (const auto& key : body.getMemberNames()) {
        if (!meta.hasColumn(key)) {
            errors.push_back({key, "UNKNOWN_FIELD",
                "Field '" + key + "' does not exist in table '" + meta.name + "'"});
            continue;
        }

        const auto* col = meta.getColumn(key);
        if (!col) continue;

        if (col->isPrimaryKey) {
            errors.push_back({col->name, "IMMUTABLE",
                "Primary key field '" + col->name + "' cannot be updated"});
            continue;
        }

        if (body[key].isNull()) continue;

        if (!isTypeCompatible(body[key], *col)) {
            errors.push_back({col->name, "INVALID_TYPE",
                "Field '" + col->name + "' expects type " + col->rawType});
        }

        if (exceedsMaxLength(body[key], *col)) {
            errors.push_back({col->name, "TOO_LONG",
                "Field '" + col->name + "' exceeds max length " +
                std::to_string(*col->maxLength)});
        }
    }

    return errors;
}

bool RequestValidator::isTypeCompatible(const Json::Value& value, const ColumnMeta& col) {
    if (value.isNull() && col.isNullable) return true;

    switch (col.jsonType) {
        case JsonType::Number:  return value.isNumeric();
        case JsonType::String:  return value.isString();
        case JsonType::Boolean: return value.isBool();
        case JsonType::Object:  return value.isObject();
        case JsonType::Array:   return value.isArray();
        case JsonType::Null:    return true;
        default:                return true;
    }
}

bool RequestValidator::exceedsMaxLength(const Json::Value& value, const ColumnMeta& col) {
    if (!col.maxLength.has_value()) return false;
    if (!value.isString()) return false;
    return value.asString().length() > static_cast<size_t>(*col.maxLength);
}

Json::Value RequestValidator::errorsToJson(const std::vector<ValidationError>& errors) {
    Json::Value arr(Json::arrayValue);
    for (const auto& error : errors) {
        Json::Value e;
        e["field"] = error.field;
        e["code"] = error.code;
        e["message"] = error.message;
        arr.append(e);
    }
    return arr;
}
