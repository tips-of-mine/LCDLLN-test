/**
 * @file LayoutExport.cpp
 * @brief layout.json export: instances + volumes, stable sort (M12.4).
 */

#include "engine/world/LayoutExport.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::world {

namespace {

/** @brief Pair (assetId, index) for stable instance sort. */
struct InstanceSortKey {
    uint32_t assetId = 0;
    size_t index = 0;
    bool operator<(const InstanceSortKey& o) const {
        if (assetId != o.assetId) return assetId < o.assetId;
        return index < o.index;
    }
};

/** @brief Pair (type, index) for stable volume sort. */
struct VolumeSortKey {
    VolumeType type = VolumeType::Trigger;
    size_t index = 0;
    bool operator<(const VolumeSortKey& o) const {
        if (static_cast<uint8_t>(type) != static_cast<uint8_t>(o.type))
            return static_cast<uint8_t>(type) < static_cast<uint8_t>(o.type);
        return index < o.index;
    }
};

const char* VolumeTypeToStr(VolumeType t) {
    switch (t) {
        case VolumeType::Trigger: return "trigger";
        case VolumeType::SpawnArea: return "spawnArea";
        case VolumeType::ZoneTransition: return "zoneTransition";
    }
    return "trigger";
}

const char* VolumeShapeToStr(VolumeShape s) {
    return (s == VolumeShape::Sphere) ? "sphere" : "box";
}

} // namespace

bool WriteLayoutJson(const std::string& path,
                    const std::vector<ZoneChunkInstance>& instances,
                    const std::vector<GameplayVolume>& volumes) {
    std::vector<InstanceSortKey> iKeys;
    iKeys.reserve(instances.size());
    for (size_t i = 0; i < instances.size(); ++i)
        iKeys.push_back({ instances[i].assetId, i });
    std::sort(iKeys.begin(), iKeys.end());

    std::vector<VolumeSortKey> vKeys;
    vKeys.reserve(volumes.size());
    for (size_t i = 0; i < volumes.size(); ++i)
        vKeys.push_back({ volumes[i].type, i });
    std::sort(vKeys.begin(), vKeys.end());

    nlohmann::json j;
    j["version"] = kLayoutJsonVersion;
    j["instances"] = nlohmann::json::array();
    for (const auto& k : iKeys) {
        const auto& inst = instances[k.index];
        nlohmann::json o;
        std::ostringstream guid;
        guid << "inst_" << inst.assetId << "_" << k.index;
        o["guid"] = guid.str();
        o["position"] = { inst.transform[12], inst.transform[13], inst.transform[14] };
        o["mesh"] = "";
        o["material"] = "";
        o["assetId"] = inst.assetId;
        j["instances"].push_back(o);
    }
    j["volumes"] = nlohmann::json::array();
    for (const auto& k : vKeys) {
        const auto& vol = volumes[k.index];
        nlohmann::json v;
        v["type"] = VolumeTypeToStr(vol.type);
        v["shape"] = VolumeShapeToStr(vol.shape);
        v["position"] = { vol.position[0], vol.position[1], vol.position[2] };
        v["halfExtents"] = { vol.halfExtents[0], vol.halfExtents[1], vol.halfExtents[2] };
        v["radius"] = vol.radius;
        if (!vol.actionId.empty()) v["actionId"] = vol.actionId;
        j["volumes"].push_back(v);
    }

    std::ofstream f(path);
    if (!f.is_open()) return false;
    try {
        f << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace engine::world
