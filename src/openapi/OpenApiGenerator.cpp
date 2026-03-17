// Author: Udaykiran Atta
// License: MIT

#include "openapi/OpenApiGenerator.h"
#include "schema/ModelRegistry.h"
#include "config/EnvConfig.h"

#include <drogon/drogon.h>

void OpenApiGenerator::registerRoutes() {
    // Serve OpenAPI spec as JSON
    drogon::app().registerHandler(
        "/api/docs/openapi.json",
        [](drogon::HttpRequestPtr req)
            -> drogon::Task<drogon::HttpResponsePtr> {
            auto spec = generateSpec();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(spec);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            co_return resp;
        },
        {drogon::Get});

    // Serve Swagger UI (redirect to SwaggerUI CDN with our spec URL)
    drogon::app().registerHandler(
        "/api/docs",
        [](drogon::HttpRequestPtr req)
            -> drogon::Task<drogon::HttpResponsePtr> {
            auto& config = EnvConfig::instance();
            auto host = config.get("HOST", "0.0.0.0");
            auto port = config.getInt("PORT", 8080);
            std::string specUrl = "http://" + host + ":" +
                std::to_string(port) + "/api/docs/openapi.json";

            std::string html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Drogon Blueprint API</title>
    <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css">
</head>
<body>
    <div id="swagger-ui"></div>
    <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
    <script>
        SwaggerUIBundle({
            url: ")" + specUrl + R"(",
            dom_id: '#swagger-ui',
            presets: [SwaggerUIBundle.presets.apis, SwaggerUIBundle.SwaggerUIStandalonePreset],
            layout: "StandaloneLayout"
        });
    </script>
</body>
</html>)";

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeCode(drogon::CT_TEXT_HTML);
            resp->setBody(html);
            co_return resp;
        },
        {drogon::Get});

    LOG_INFO << "OpenAPI docs registered at /api/docs";
}

Json::Value OpenApiGenerator::generateSpec() {
    Json::Value spec;
    spec["openapi"] = "3.0.3";
    spec["info"] = generateInfo();

    auto& config = EnvConfig::instance();
    Json::Value server;
    server["url"] = "http://" + config.get("HOST", "0.0.0.0") + ":" +
        std::to_string(config.getInt("PORT", 8080));
    server["description"] = config.get("ENVIRONMENT", "development");
    spec["servers"] = Json::Value(Json::arrayValue);
    spec["servers"].append(server);

    spec["paths"] = generatePaths();
    spec["components"] = generateComponents();
    return spec;
}

Json::Value OpenApiGenerator::generateInfo() {
    Json::Value info;
    info["title"] = "Drogon Blueprint API";
    info["description"] = "Auto-generated REST API from database schema";
    info["version"] = "0.1.0";

    Json::Value contact;
    contact["name"] = "Blueprint Framework";
    info["contact"] = contact;

    return info;
}

Json::Value OpenApiGenerator::generatePaths() {
    Json::Value paths;
    auto& registry = ModelRegistry::instance();

    for (const auto& name : registry.getTableNames()) {
        const auto* meta = registry.getTable(name);
        if (!meta) continue;

        std::string listPath = "/api/v1/" + name;
        std::string itemPath = "/api/v1/" + name + "/{id}";
        std::string bulkPath = "/api/v1/" + name + "/bulk";

        paths[listPath] = generateTablePaths(*meta);
        paths[itemPath] = generateItemPaths(*meta);

        // Bulk create endpoint
        Json::Value bulkPost;
        bulkPost["tags"] = Json::Value(Json::arrayValue);
        bulkPost["tags"].append(name);
        bulkPost["summary"] = "Bulk create " + name;
        bulkPost["operationId"] = "bulkCreate_" + name;

        Json::Value reqBody;
        reqBody["required"] = true;
        Json::Value content;
        Json::Value schema;
        schema["type"] = "array";
        schema["items"]["$ref"] = "#/components/schemas/" + name + "_create";
        content["application/json"]["schema"] = schema;
        reqBody["content"] = content;
        bulkPost["requestBody"] = reqBody;

        Json::Value resp201;
        resp201["description"] = "Bulk creation result";
        bulkPost["responses"]["201"] = resp201;
        paths[bulkPath]["post"] = bulkPost;
    }

    // Health endpoint
    Json::Value healthGet;
    healthGet["tags"] = Json::Value(Json::arrayValue);
    healthGet["tags"].append("system");
    healthGet["summary"] = "Health check";
    healthGet["operationId"] = "healthCheck";
    Json::Value healthResp;
    healthResp["description"] = "Health status";
    healthGet["responses"]["200"] = healthResp;
    paths["/health"]["get"] = healthGet;

    return paths;
}

Json::Value OpenApiGenerator::generateTablePaths(const TableMeta& meta) {
    Json::Value path;

    // GET - list with pagination
    Json::Value get;
    get["tags"] = Json::Value(Json::arrayValue);
    get["tags"].append(meta.name);
    get["summary"] = "List " + meta.name;
    get["operationId"] = "list_" + meta.name;

    Json::Value params(Json::arrayValue);

    Json::Value limitParam;
    limitParam["name"] = "limit";
    limitParam["in"] = "query";
    limitParam["schema"]["type"] = "integer";
    limitParam["schema"]["default"] = 20;
    limitParam["description"] = "Number of records to return";
    params.append(limitParam);

    Json::Value offsetParam;
    offsetParam["name"] = "offset";
    offsetParam["in"] = "query";
    offsetParam["schema"]["type"] = "integer";
    offsetParam["schema"]["default"] = 0;
    offsetParam["description"] = "Number of records to skip";
    params.append(offsetParam);

    Json::Value sortParam;
    sortParam["name"] = "sort";
    sortParam["in"] = "query";
    sortParam["schema"]["type"] = "string";
    sortParam["description"] = "Sort column and direction (e.g., created_at:desc)";
    params.append(sortParam);

    // Filter params for each column
    for (const auto& col : meta.columns) {
        Json::Value filterParam;
        filterParam["name"] = "filter[" + col.name + "]";
        filterParam["in"] = "query";
        filterParam["schema"]["type"] = "string";
        filterParam["description"] = "Filter by " + col.name;
        filterParam["required"] = false;
        params.append(filterParam);
    }

    get["parameters"] = params;
    get["responses"]["200"] = generatePaginatedResponse(meta.name);
    path["get"] = get;

    // POST - create
    Json::Value post;
    post["tags"] = Json::Value(Json::arrayValue);
    post["tags"].append(meta.name);
    post["summary"] = "Create " + meta.name;
    post["operationId"] = "create_" + meta.name;

    Json::Value reqBody;
    reqBody["required"] = true;
    Json::Value content;
    content["application/json"]["schema"]["$ref"] =
        "#/components/schemas/" + meta.name + "_create";
    reqBody["content"] = content;
    post["requestBody"] = reqBody;

    Json::Value created;
    created["description"] = "Created";
    Json::Value createdContent;
    createdContent["application/json"]["schema"]["$ref"] =
        "#/components/schemas/" + meta.name;
    created["content"] = createdContent;
    post["responses"]["201"] = created;
    post["responses"]["422"] = generateErrorResponse();
    path["post"] = post;

    return path;
}

Json::Value OpenApiGenerator::generateItemPaths(const TableMeta& meta) {
    Json::Value path;

    Json::Value idParam;
    idParam["name"] = "id";
    idParam["in"] = "path";
    idParam["required"] = true;
    idParam["schema"]["type"] = "string";
    idParam["description"] = "Primary key value";
    Json::Value params(Json::arrayValue);
    params.append(idParam);

    // GET by ID
    Json::Value get;
    get["tags"] = Json::Value(Json::arrayValue);
    get["tags"].append(meta.name);
    get["summary"] = "Get " + meta.name + " by ID";
    get["operationId"] = "get_" + meta.name;
    get["parameters"] = params;

    Json::Value okResp;
    okResp["description"] = "Success";
    Json::Value okContent;
    okContent["application/json"]["schema"]["$ref"] =
        "#/components/schemas/" + meta.name;
    okResp["content"] = okContent;
    get["responses"]["200"] = okResp;

    Json::Value notFound;
    notFound["description"] = "Not found";
    get["responses"]["404"] = notFound;
    path["get"] = get;

    // PUT - update
    Json::Value put;
    put["tags"] = Json::Value(Json::arrayValue);
    put["tags"].append(meta.name);
    put["summary"] = "Update " + meta.name;
    put["operationId"] = "update_" + meta.name;
    put["parameters"] = params;

    Json::Value updateBody;
    updateBody["required"] = true;
    Json::Value updateContent;
    updateContent["application/json"]["schema"]["$ref"] =
        "#/components/schemas/" + meta.name + "_update";
    updateBody["content"] = updateContent;
    put["requestBody"] = updateBody;
    put["responses"]["200"] = okResp;
    put["responses"]["404"] = notFound;
    put["responses"]["422"] = generateErrorResponse();
    path["put"] = put;

    // DELETE
    Json::Value del;
    del["tags"] = Json::Value(Json::arrayValue);
    del["tags"].append(meta.name);
    del["summary"] = "Delete " + meta.name;
    del["operationId"] = "delete_" + meta.name;
    del["parameters"] = params;
    del["responses"]["200"] = okResp;
    del["responses"]["404"] = notFound;
    path["delete"] = del;

    return path;
}

Json::Value OpenApiGenerator::generateComponents() {
    Json::Value components;
    Json::Value schemas;
    auto& registry = ModelRegistry::instance();

    for (const auto& name : registry.getTableNames()) {
        const auto* meta = registry.getTable(name);
        if (!meta) continue;

        schemas[name] = generateTableSchema(*meta);
        schemas[name + "_create"] = generateCreateSchema(*meta);
        schemas[name + "_update"] = generateUpdateSchema(*meta);
    }

    // Error schema
    Json::Value errorSchema;
    errorSchema["type"] = "object";
    errorSchema["properties"]["error"]["type"] = "object";
    errorSchema["properties"]["error"]["properties"]["message"]["type"] = "string";
    errorSchema["properties"]["error"]["properties"]["status"]["type"] = "integer";
    errorSchema["properties"]["error"]["properties"]["details"]["type"] = "object";
    schemas["Error"] = errorSchema;

    // Pagination meta
    Json::Value paginationMeta;
    paginationMeta["type"] = "object";
    paginationMeta["properties"]["total"]["type"] = "integer";
    paginationMeta["properties"]["limit"]["type"] = "integer";
    paginationMeta["properties"]["offset"]["type"] = "integer";
    paginationMeta["properties"]["has_more"]["type"] = "boolean";
    schemas["PaginationMeta"] = paginationMeta;

    components["schemas"] = schemas;

    // Security scheme
    Json::Value securitySchemes;
    Json::Value bearerAuth;
    bearerAuth["type"] = "http";
    bearerAuth["scheme"] = "bearer";
    bearerAuth["bearerFormat"] = "JWT";
    securitySchemes["bearerAuth"] = bearerAuth;
    components["securitySchemes"] = securitySchemes;

    return components;
}

Json::Value OpenApiGenerator::generateTableSchema(const TableMeta& meta) {
    Json::Value schema;
    schema["type"] = "object";

    Json::Value properties;
    Json::Value required(Json::arrayValue);

    for (const auto& col : meta.columns) {
        properties[col.name] = generateColumnSchema(col);
        if (!col.isNullable && !col.isAutoIncrement && !col.defaultValue.has_value()) {
            required.append(col.name);
        }
    }

    schema["properties"] = properties;
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

Json::Value OpenApiGenerator::generateColumnSchema(const ColumnMeta& col) {
    Json::Value schema;
    schema["type"] = sqlTypeToOpenApiType(col.sqlType);

    auto format = sqlTypeToOpenApiFormat(col.sqlType);
    if (!format.empty()) {
        schema["format"] = format;
    }

    if (col.isNullable) {
        schema["nullable"] = true;
    }

    if (col.maxLength.has_value()) {
        schema["maxLength"] = *col.maxLength;
    }

    if (col.defaultValue.has_value()) {
        schema["default"] = *col.defaultValue;
    }

    return schema;
}

Json::Value OpenApiGenerator::generateCreateSchema(const TableMeta& meta) {
    Json::Value schema;
    schema["type"] = "object";

    static const std::vector<std::string> autoFields = {
        "id", "code", "created_at", "updated_at",
        "created_by", "modified_by", "deleted_at", "deleted_by"
    };

    Json::Value properties;
    Json::Value required(Json::arrayValue);

    for (const auto& col : meta.columns) {
        if (col.isAutoIncrement) continue;
        bool isAuto = false;
        for (const auto& af : autoFields) {
            if (col.name == af) { isAuto = true; break; }
        }
        if (isAuto) continue;

        properties[col.name] = generateColumnSchema(col);
        if (!col.isNullable && !col.defaultValue.has_value()) {
            required.append(col.name);
        }
    }

    schema["properties"] = properties;
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

Json::Value OpenApiGenerator::generateUpdateSchema(const TableMeta& meta) {
    Json::Value schema;
    schema["type"] = "object";

    Json::Value properties;
    for (const auto& col : meta.columns) {
        if (col.isPrimaryKey) continue;
        properties[col.name] = generateColumnSchema(col);
    }

    schema["properties"] = properties;
    return schema;
}

Json::Value OpenApiGenerator::generatePaginatedResponse(
    const std::string& table) {
    Json::Value resp;
    resp["description"] = "Paginated list";

    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["data"]["type"] = "array";
    schema["properties"]["data"]["items"]["$ref"] =
        "#/components/schemas/" + table;
    schema["properties"]["meta"]["$ref"] = "#/components/schemas/PaginationMeta";

    Json::Value content;
    content["application/json"]["schema"] = schema;
    resp["content"] = content;
    return resp;
}

Json::Value OpenApiGenerator::generateErrorResponse() {
    Json::Value resp;
    resp["description"] = "Validation error";
    Json::Value content;
    content["application/json"]["schema"]["$ref"] = "#/components/schemas/Error";
    resp["content"] = content;
    return resp;
}

std::string OpenApiGenerator::sqlTypeToOpenApiType(SqlType type) {
    switch (type) {
        case SqlType::Integer:  return "integer";
        case SqlType::Float:
        case SqlType::Decimal:  return "number";
        case SqlType::Boolean:  return "boolean";
        case SqlType::Json:     return "object";
        default:                return "string";
    }
}

std::string OpenApiGenerator::sqlTypeToOpenApiFormat(SqlType type) {
    switch (type) {
        case SqlType::Integer:  return "int64";
        case SqlType::Float:    return "float";
        case SqlType::Decimal:  return "double";
        case SqlType::DateTime: return "date-time";
        case SqlType::Date:     return "date";
        case SqlType::Time:     return "time";
        case SqlType::Uuid:     return "uuid";
        case SqlType::Binary:   return "binary";
        default:                return "";
    }
}
