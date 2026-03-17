// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <string>
#include "Types.h"

struct TypeMapping {
    SqlType sqlType;
    JsonType jsonType;
};

class TypeMapper {
public:
    static TypeMapping mapMySQLType(const std::string& dataType, const std::string& columnType);
    static TypeMapping mapPostgresType(const std::string& dataType, const std::string& udtName);
    static TypeMapping mapSQLiteType(const std::string& declaredType);

private:
    static std::string toUpper(const std::string& s);
};
