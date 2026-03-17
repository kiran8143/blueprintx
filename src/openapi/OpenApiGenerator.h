// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <json/json.h>
#include <string>
#include "schema/Types.h"

// Generates OpenAPI 3.0 specification from the ModelRegistry.
// Serves the spec at /api/docs/openapi.json and Swagger UI at /api/docs.
class OpenApiGenerator {
public:
    // Generate the full OpenAPI 3.0 spec from all registered tables
    static Json::Value generateSpec();

    // Register the /api/docs and /api/docs/openapi.json endpoints
    static void registerRoutes();

private:
    static Json::Value generateInfo();
    static Json::Value generatePaths();
    static Json::Value generateComponents();

    // Per-table path generation
    static Json::Value generateTablePaths(const TableMeta& meta);
    static Json::Value generateItemPaths(const TableMeta& meta);

    // Schema generation from table metadata
    static Json::Value generateTableSchema(const TableMeta& meta);
    static Json::Value generateColumnSchema(const ColumnMeta& col);
    static Json::Value generateCreateSchema(const TableMeta& meta);
    static Json::Value generateUpdateSchema(const TableMeta& meta);

    // Response schemas
    static Json::Value generatePaginatedResponse(const std::string& table);
    static Json::Value generateErrorResponse();

    static std::string sqlTypeToOpenApiType(SqlType type);
    static std::string sqlTypeToOpenApiFormat(SqlType type);
};
