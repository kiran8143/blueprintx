// Author: Udaykiran Atta
// License: MIT

#include "protection/CodeGenerator.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <drogon/drogon.h>

// -----------------------------------------------------------------------
// Thread-local RNG
// -----------------------------------------------------------------------

namespace {

/// Return a thread-local mt19937_64 seeded from the OS entropy source.
std::mt19937_64& threadLocalRng() {
    thread_local std::mt19937_64 rng(std::random_device{}());
    return rng;
}

} // anonymous namespace

// -----------------------------------------------------------------------
// UUID v4 generation
// -----------------------------------------------------------------------

std::string CodeGenerator::generateUuid() {
    auto& rng = threadLocalRng();
    std::uniform_int_distribution<uint32_t> dist(0, 15);
    std::uniform_int_distribution<uint32_t> dist2(8, 11); // for variant bits

    // UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // 32 hex digits + 4 hyphens = 36 chars
    // Position  12 is always '4'  (version)
    // Position  16 is one of 8,9,a,b (variant)

    constexpr const char* hex = "0123456789abcdef";
    // Template with hyphens at positions 8, 13, 18, 23
    // Total hex positions: 0-7, 9-12, 14-17, 19-22, 24-35
    // That's 32 hex digits.

    std::string uuid(36, '-');

    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            // hyphen -- already set
            continue;
        }

        if (i == 14) {
            // version nibble: always 4
            uuid[i] = '4';
        } else if (i == 19) {
            // variant nibble: 8, 9, a, or b
            uuid[i] = hex[dist2(rng)];
        } else {
            uuid[i] = hex[dist(rng)];
        }
    }

    return uuid;
}

// -----------------------------------------------------------------------
// Code injection
// -----------------------------------------------------------------------

void CodeGenerator::injectCode(Json::Value& data, const TableMeta& meta) {
    if (!meta.hasColumn("code")) {
        return;
    }

    // Do not overwrite a value the caller already provided.
    if (data.isMember("code") && !data["code"].isNull() &&
        !(data["code"].isString() && data["code"].asString().empty())) {
        LOG_DEBUG << "CodeGenerator: 'code' already provided for table '"
                  << meta.name << "', skipping injection";
        return;
    }

    const auto uuid = generateUuid();
    data["code"] = uuid;

    LOG_DEBUG << "CodeGenerator: injected code '" << uuid
              << "' for table '" << meta.name << "'";
}
