// Author: Udaykiran Atta
// License: MIT

#include "schema/TypeMapper.h"
#include <algorithm>
#include <cctype>

std::string TypeMapper::toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

TypeMapping TypeMapper::mapMySQLType(const std::string& dataType, const std::string& columnType) {
    auto dt = toUpper(dataType);

    // MySQL BOOLEAN is tinyint(1) — must check columnType
    if (dt == "TINYINT") {
        if (columnType.find("tinyint(1)") != std::string::npos) {
            return {SqlType::Boolean, JsonType::Boolean};
        }
        return {SqlType::Integer, JsonType::Number};
    }

    if (dt == "INT" || dt == "BIGINT" || dt == "SMALLINT" || dt == "MEDIUMINT")
        return {SqlType::Integer, JsonType::Number};
    if (dt == "FLOAT" || dt == "DOUBLE")
        return {SqlType::Float, JsonType::Number};
    if (dt == "DECIMAL" || dt == "NUMERIC")
        return {SqlType::Decimal, JsonType::Number};
    if (dt == "VARCHAR" || dt == "CHAR" || dt == "TEXT" ||
        dt == "MEDIUMTEXT" || dt == "LONGTEXT" || dt == "TINYTEXT" ||
        dt == "ENUM" || dt == "SET")
        return {SqlType::String, JsonType::String};
    if (dt == "DATETIME" || dt == "TIMESTAMP")
        return {SqlType::DateTime, JsonType::String};
    if (dt == "DATE")
        return {SqlType::Date, JsonType::String};
    if (dt == "TIME")
        return {SqlType::Time, JsonType::String};
    if (dt == "JSON")
        return {SqlType::Json, JsonType::Object};
    if (dt == "BLOB" || dt == "MEDIUMBLOB" || dt == "LONGBLOB" ||
        dt == "TINYBLOB" || dt == "BINARY" || dt == "VARBINARY")
        return {SqlType::Binary, JsonType::String};

    return {SqlType::Unknown, JsonType::String};
}

TypeMapping TypeMapper::mapPostgresType(const std::string& dataType, const std::string& udtName) {
    auto udt = toUpper(udtName);

    if (udt == "BOOL")
        return {SqlType::Boolean, JsonType::Boolean};
    if (udt == "INT2" || udt == "INT4" || udt == "INT8" ||
        udt == "SERIAL" || udt == "BIGSERIAL")
        return {SqlType::Integer, JsonType::Number};
    if (udt == "FLOAT4" || udt == "FLOAT8")
        return {SqlType::Float, JsonType::Number};
    if (udt == "NUMERIC")
        return {SqlType::Decimal, JsonType::Number};
    if (udt == "UUID")
        return {SqlType::Uuid, JsonType::String};
    if (udt == "JSON" || udt == "JSONB")
        return {SqlType::Json, JsonType::Object};
    if (udt == "TIMESTAMP" || udt == "TIMESTAMPTZ")
        return {SqlType::DateTime, JsonType::String};
    if (udt == "DATE")
        return {SqlType::Date, JsonType::String};
    if (udt == "TIME" || udt == "TIMETZ")
        return {SqlType::Time, JsonType::String};
    if (udt == "BYTEA")
        return {SqlType::Binary, JsonType::String};

    // Fall back to data_type for text types
    auto dt = toUpper(dataType);
    if (dt == "CHARACTER VARYING" || dt == "CHARACTER" || dt == "TEXT")
        return {SqlType::String, JsonType::String};

    return {SqlType::Unknown, JsonType::String};
}

TypeMapping TypeMapper::mapSQLiteType(const std::string& declaredType) {
    auto upper = toUpper(declaredType);

    // SQLite type affinity rules (section 3.1)
    if (upper.find("INT") != std::string::npos)
        return {SqlType::Integer, JsonType::Number};
    if (upper.find("CHAR") != std::string::npos ||
        upper.find("CLOB") != std::string::npos ||
        upper.find("TEXT") != std::string::npos)
        return {SqlType::String, JsonType::String};
    if (upper.find("BLOB") != std::string::npos || upper.empty())
        return {SqlType::Binary, JsonType::String};
    if (upper.find("REAL") != std::string::npos ||
        upper.find("FLOA") != std::string::npos ||
        upper.find("DOUB") != std::string::npos)
        return {SqlType::Float, JsonType::Number};
    if (upper.find("BOOL") != std::string::npos)
        return {SqlType::Boolean, JsonType::Boolean};
    if (upper.find("DATE") != std::string::npos ||
        upper.find("TIME") != std::string::npos)
        return {SqlType::DateTime, JsonType::String};

    // Default: NUMERIC affinity
    return {SqlType::Decimal, JsonType::Number};
}
