// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <json/json.h>
#include <string>
#include "schema/Types.h"

/// UUID v4 code generation for tables that carry a "code" column.
///
/// Generates RFC 4122 compliant version-4 UUIDs using a thread-local
/// Mersenne Twister seeded from std::random_device.
class CodeGenerator {
public:
    /// Generate a random UUID v4 string.
    /// Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    /// where y is one of {8, 9, a, b}.
    static std::string generateUuid();

    /// If the table has a "code" column and the caller has not already
    /// supplied one in `data`, generate and inject a UUID.
    static void injectCode(Json::Value& data, const TableMeta& meta);
};
