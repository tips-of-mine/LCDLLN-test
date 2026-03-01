/**
 * @file ZoneBuildFormat.cpp
 * @brief Reader for zone_builder output (M11.2, M11.5 versioned).
 */

#include "engine/world/ZoneBuildFormat.h"
#include "engine/world/VersionedHeader.h"

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
    uint32_t magic = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || magic != kZoneMetaMagic) {
        std::fclose(f);
        return false;
    }
    VersionedHeader vh;
    if (std::fread(&vh.formatVersion, 4, 1, f) != 1 || std::fread(&vh.builderVersion, 4, 1, f) != 1 ||
        std::fread(&vh.engineVersion, 4, 1, f) != 1 || std::fread(&vh.contentHash, 8, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (!ValidateVersionedHeader(vh, "zone.meta")) {
        std::fclose(f);
        return false;
    }
    uint32_t numChunks = 0;
    if (std::fread(&zoneId, 1, 4, f) != 4 || std::fread(&numChunks, 1, 4, f) != 4) {
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
    uint32_t magic = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || magic != kZoneChunkMetaMagic) {
        std::fclose(f);
        return false;
    }
    VersionedHeader vh;
    if (std::fread(&vh.formatVersion, 4, 1, f) != 1 || std::fread(&vh.builderVersion, 4, 1, f) != 1 ||
        std::fread(&vh.engineVersion, 4, 1, f) != 1 || std::fread(&vh.contentHash, 8, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (!ValidateVersionedHeader(vh, "chunk.meta")) {
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
    uint32_t magic = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || magic != kInstancesBinMagic) {
        std::fclose(f);
        return false;
    }
    VersionedHeader vh;
    if (std::fread(&vh.formatVersion, 4, 1, f) != 1 || std::fread(&vh.builderVersion, 4, 1, f) != 1 ||
        std::fread(&vh.engineVersion, 4, 1, f) != 1 || std::fread(&vh.contentHash, 8, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (!ValidateVersionedHeader(vh, "instances.bin")) {
        std::fclose(f);
        return false;
    }
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

bool WriteZoneChunkInstances(const std::string& path, const std::vector<ZoneChunkInstance>& instances, const VersionedHeader& vh) {
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
#endif
    uint32_t magic = kInstancesBinMagic;
    if (std::fwrite(&magic, 1, 4, f) != 4) { std::fclose(f); return false; }
    if (std::fwrite(&vh.formatVersion, 4, 1, f) != 1 || std::fwrite(&vh.builderVersion, 4, 1, f) != 1 ||
        std::fwrite(&vh.engineVersion, 4, 1, f) != 1 || std::fwrite(&vh.contentHash, 8, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    const uint32_t n = static_cast<uint32_t>(instances.size());
    if (std::fwrite(&n, 1, 4, f) != 4) { std::fclose(f); return false; }
    for (const auto& inst : instances) {
        if (std::fwrite(inst.transform, 4, 16, f) != 16 ||
            std::fwrite(&inst.assetId, 1, 4, f) != 4 ||
            std::fwrite(&inst.flags, 1, 4, f) != 4) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

} // namespace engine::world
