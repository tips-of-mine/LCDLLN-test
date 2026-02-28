/**
 * @file ProbesFormat.cpp
 * @brief Reader for probes.bin and zone_atmosphere.json (M11.4, M11.5 versioned).
 */

#include "engine/world/ProbesFormat.h"
#include "engine/world/VersionedHeader.h"

#include <cstdio>
#include <cstring>

namespace engine::world {

bool ReadProbesBin(const std::string& path, std::vector<ZoneProbe>& out) {
    out.clear();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || magic != kProbesBinMagic) {
        std::fclose(f);
        return false;
    }
    VersionedHeader vh;
    if (std::fread(&vh.formatVersion, 4, 1, f) != 1 || std::fread(&vh.builderVersion, 4, 1, f) != 1 ||
        std::fread(&vh.engineVersion, 4, 1, f) != 1 || std::fread(&vh.contentHash, 8, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (!ValidateVersionedHeader(vh, "probes.bin")) {
        std::fclose(f);
        return false;
    }
    uint32_t numProbes = 0;
    if (std::fread(&numProbes, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    out.resize(numProbes);
    for (uint32_t i = 0; i < numProbes; ++i) {
        if (std::fread(out[i].position, 4, 3, f) != 3 ||
            std::fread(&out[i].radius, 4, 1, f) != 1 ||
            std::fread(&out[i].intensity, 4, 1, f) != 1) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool ReadZoneAtmosphere(const std::string& path, ZoneAtmosphere& out) {
    out.skyColor[0] = 0.53f;
    out.skyColor[1] = 0.81f;
    out.skyColor[2] = 1.0f;
    out.horizonExponent = 1.f;
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    std::fclose(f);
    return true;
}

} // namespace engine::world
