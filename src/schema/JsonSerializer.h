// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <drogon/orm/Result.h>
#include <json/json.h>
#include "Types.h"

class JsonSerializer {
public:
    static Json::Value serializeRow(
        const drogon::orm::Row& row,
        const TableMeta& meta);

    static Json::Value serializeResult(
        const drogon::orm::Result& result,
        const TableMeta& meta);

    static Json::Value serializeRowRaw(
        const drogon::orm::Row& row);
};
