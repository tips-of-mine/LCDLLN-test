/**
 * @file ZoneBuildFormat.cpp
 * @brief Reader for zone_builder output (M11.2).
 */

#include "engine/world/ZoneBuildFormat.h"

#include <cstdio>
#include <cstring>

namespace engine::world {

bool ReadZoneMeta(const std::string& path, int32_t& zoneId, std::vector<std::pair<int32_t, int32_t>>& chunkCoords) {
    chunkCoords.clear();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0;
    uint32_t numChunks = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || std::fread(&version, 1, 4, f) != 4 ||
        std::fread(&zoneId, 1, 4, f) != 4 || std::fread(&numChunks, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    if (magic != kZoneMetaMagic || version != 1u) {
        std::fclose(f);
        return false;
    }
    chunkCoords.resize(numChunks);
    for (uint32_t i = 0; i < numChunks; ++i) {
        if (std::fread(&chunkCoords[i].first, 1, 4, f) != 4 || std::fread(&chunkCoords[i].second, 1, 4, f) != 4) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool ReadZoneChunkMeta(const std::string& path, ZoneChunkMeta& out) {
    std::memset(&out, 0, sizeof(out));
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || std::fread(&version, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    if (magic != kZoneChunkMetaMagic || version != 1u) {
        std::fclose(f);
        return false;
    }
    if (std::fread(out.boundsMin, 4, 3, f) != 3 || std::fread(out.boundsMax, 4, 3, f) != 3 || std::fread(&out.flags, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

bool ReadZoneChunkInstances(const std::string& path, std::vector<ZoneChunkInstance>& out) {
    out.clear();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t n = 0;
    if (std::fread(&n, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (std::fread(out[i].transform, 4, 16, f) != 16 ||
            std::fread(&out[i].assetId, 1, 4, f) != 4 ||
            std::fread(&out[i].flags, 1, 4, f) != 4) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

} // namespace engine::world
