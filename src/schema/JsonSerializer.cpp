// Author: Udaykiran Atta
// License: MIT

#include "schema/JsonSerializer.h"

#include <drogon/orm/Row.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/ResultIterator.h>
#include <sstream>

Json::Value JsonSerializer::serializeRow(
    const drogon::orm::Row& row,
    const TableMeta& meta) {
    Json::Value obj(Json::objectValue);

    for (size_t i = 0; i < row.size(); ++i) {
        const auto& field = row[i];
        std::string colName = field.name();

        if (field.isNull()) {
            obj[colName] = Json::nullValue;
            continue;
        }

        const auto* colMeta = meta.getColumn(colName);
        if (!colMeta) {
            obj[colName] = field.as<std::string>();
            continue;
        }

        switch (colMeta->sqlType) {
            case SqlType::Integer:
                obj[colName] = static_cast<Json::Int64>(field.as<int64_t>());
                break;
            case SqlType::Float:
            case SqlType::Decimal:
                obj[colName] = field.as<double>();
                break;
            case SqlType::Boolean:
                obj[colName] = field.as<bool>();
                break;
            case SqlType::Json: {
                static thread_local Json::CharReaderBuilder builder;
                Json::Value parsed;
                std::string raw = field.as<std::string>();
                std::string errors;
                std::istringstream stream(raw);
                if (Json::parseFromStream(builder, stream, &parsed, &errors)) {
                    obj[colName] = parsed;
                } else {
                    obj[colName] = raw;
                }
                break;
            }
            default:
                obj[colName] = field.as<std::string>();
                break;
        }
    }

    return obj;
}

Json::Value JsonSerializer::serializeResult(
    const drogon::orm::Result& result,
    const TableMeta& meta) {
    Json::Value arr(Json::arrayValue);
    for (const auto& row : result) {
        arr.append(serializeRow(row, meta));
    }
    return arr;
}

Json::Value JsonSerializer::serializeRowRaw(
    const drogon::orm::Row& row) {
    Json::Value obj(Json::objectValue);
    for (size_t i = 0; i < row.size(); ++i) {
        const auto& field = row[i];
        std::string colName = field.name();
        if (field.isNull()) {
            obj[colName] = Json::nullValue;
        } else {
            obj[colName] = field.as<std::string>();
        }
    }
    return obj;
}
