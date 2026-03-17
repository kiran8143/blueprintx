// Author: Udaykiran Atta
// License: MIT

#include "protection/AuditInjector.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <drogon/drogon.h>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

std::string AuditInjector::nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);

    std::tm utc{};
    gmtime_r(&tt, &utc);

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// -----------------------------------------------------------------------
// Blind injection (no schema check)
// -----------------------------------------------------------------------

void AuditInjector::injectCreate(Json::Value& data,
                                 const std::string& userId) {
    const auto ts = nowIso8601();

    data["created_at"]  = ts;
    data["updated_at"]  = ts;
    data["created_by"]  = userId;
    data["modified_by"] = userId;

    LOG_DEBUG << "AuditInjector: injected CREATE audit fields (user="
              << userId << ", ts=" << ts << ")";
}

void AuditInjector::injectUpdate(Json::Value& data,
                                 const std::string& userId) {
    const auto ts = nowIso8601();

    data["updated_at"]  = ts;
    data["modified_by"] = userId;

    LOG_DEBUG << "AuditInjector: injected UPDATE audit fields (user="
              << userId << ", ts=" << ts << ")";
}

// -----------------------------------------------------------------------
// Schema-aware injection
// -----------------------------------------------------------------------

void AuditInjector::injectCreateWithMeta(Json::Value& data,
                                         const std::string& userId,
                                         const TableMeta& meta) {
    const auto ts = nowIso8601();

    if (meta.hasColumn("created_at")) {
        data["created_at"] = ts;
    }
    if (meta.hasColumn("updated_at")) {
        data["updated_at"] = ts;
    }
    if (meta.hasColumn("created_by")) {
        data["created_by"] = userId;
    }
    if (meta.hasColumn("modified_by")) {
        data["modified_by"] = userId;
    }

    LOG_DEBUG << "AuditInjector: injected CREATE audit fields with meta "
              << "(table=" << meta.name << ", user=" << userId << ")";
}

void AuditInjector::injectUpdateWithMeta(Json::Value& data,
                                         const std::string& userId,
                                         const TableMeta& meta) {
    const auto ts = nowIso8601();

    if (meta.hasColumn("updated_at")) {
        data["updated_at"] = ts;
    }
    if (meta.hasColumn("modified_by")) {
        data["modified_by"] = userId;
    }

    LOG_DEBUG << "AuditInjector: injected UPDATE audit fields with meta "
              << "(table=" << meta.name << ", user=" << userId << ")";
}
