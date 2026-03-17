// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <json/json.h>
#include <vector>
#include <string>
#include "Types.h"

struct ValidationError {
    std::string field;
    std::string code;
    std::string message;
};

class RequestValidator {
public:
    static std::vector<ValidationError> validateCreate(
        const Json::Value& body,
        const TableMeta& meta,
        const std::vector<std::string>& autoFields = {
            "id", "code", "created_at", "updated_at",
            "created_by", "modified_by", "deleted_at", "deleted_by"
        });

    static std::vector<ValidationError> validateUpdate(
        const Json::Value& body,
        const TableMeta& meta);

    static Json::Value errorsToJson(const std::vector<ValidationError>& errors);

private:
    static bool isTypeCompatible(const Json::Value& value, const ColumnMeta& col);
    static bool exceedsMaxLength(const Json::Value& value, const ColumnMeta& col);
};
