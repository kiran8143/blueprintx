// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <json/json.h>
#include <string>
#include "schema/Types.h"

/// Automatic audit-trail injection.
///
/// Adds created_at / updated_at / created_by / modified_by fields
/// to JSON payloads destined for INSERT or UPDATE statements.
///
/// Two flavours of every method:
///   - "blind" (no TableMeta): always injects the field.
///   - "WithMeta": only injects if the table actually has the column.
class AuditInjector {
public:
    /// Inject audit fields for a CREATE operation (blind -- no schema check).
    /// Sets: created_at, updated_at, created_by, modified_by.
    static void injectCreate(Json::Value& data,
                             const std::string& userId);

    /// Inject audit fields for an UPDATE operation (blind -- no schema check).
    /// Sets: updated_at, modified_by.
    static void injectUpdate(Json::Value& data,
                             const std::string& userId);

    /// Inject audit fields for a CREATE, only when the column exists in meta.
    static void injectCreateWithMeta(Json::Value& data,
                                     const std::string& userId,
                                     const TableMeta& meta);

    /// Inject audit fields for an UPDATE, only when the column exists in meta.
    static void injectUpdateWithMeta(Json::Value& data,
                                     const std::string& userId,
                                     const TableMeta& meta);

    /// Return the current UTC timestamp in ISO 8601 format.
    /// E.g. "2026-03-16T12:00:00Z"
    static std::string nowIso8601();
};
