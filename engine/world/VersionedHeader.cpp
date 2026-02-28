/**
 * @file VersionedHeader.cpp
 * @brief Validation of versioned headers (M11.5).
 */

#include "engine/world/VersionedHeader.h"
#include "engine/core/Log.h"

namespace engine::world {

bool ValidateVersionedHeader(const VersionedHeader& h, const char* fileType) {
    if (h.formatVersion != kZoneBuildFormatVersion) {
        LOG_ERROR(World, "{}: incompatible format version {} (expected {})", fileType ? fileType : "file", h.formatVersion, kZoneBuildFormatVersion);
        return false;
    }
    if (h.engineVersion > kCurrentEngineVersion) {
        LOG_ERROR(World, "{}: content built for newer engine version {} (current {})", fileType ? fileType : "file", h.engineVersion, kCurrentEngineVersion);
        return false;
    }
    return true;
}

} // namespace engine::world
