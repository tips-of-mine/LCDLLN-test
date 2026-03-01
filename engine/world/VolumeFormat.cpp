/**
 * @file VolumeFormat.cpp
 * @brief JSON read/write for gameplay volumes (M12.3).
 */

#include "engine/world/VolumeFormat.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace engine::world {

namespace {
const char* TypeToString(VolumeType t) {
    switch (t) {
        case VolumeType::Trigger:        return "trigger";
        case VolumeType::SpawnArea:     return "spawnArea";
        case VolumeType::ZoneTransition: return "zoneTransition";
    }
    return "trigger";
}
VolumeType StringToType(const std::string& s) {
    if (s == "spawnArea") return VolumeType::SpawnArea;
    if (s == "zoneTransition") return VolumeType::ZoneTransition;
    return VolumeType::Trigger;
}
const char* ShapeToString(VolumeShape s) {
    return (s == VolumeShape::Sphere) ? "sphere" : "box";
}
VolumeShape StringToShape(const std::string& s) {
    return (s == "sphere") ? VolumeShape::Sphere : VolumeShape::Box;
}
} // namespace

bool ReadVolumesJson(const std::string& path, std::vector<GameplayVolume>& out) {
    out.clear();
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto arr = j.find("volumes");
        if (arr == j.end() || !arr->is_array()) return true;
        for (const auto& v : *arr) {
            GameplayVolume vol;
            std::string typeStr = v.value("type", "trigger");
            vol.type = StringToType(typeStr);
            std::string shapeStr = v.value("shape", "box");
            vol.shape = StringToShape(shapeStr);
            if (v.contains("position") && v["position"].is_array() && v["position"].size() >= 3) {
                vol.position[0] = v["position"][0].get<float>();
                vol.position[1] = v["position"][1].get<float>();
                vol.position[2] = v["position"][2].get<float>();
            }
            if (v.contains("halfExtents") && v["halfExtents"].is_array() && v["halfExtents"].size() >= 3) {
                vol.halfExtents[0] = v["halfExtents"][0].get<float>();
                vol.halfExtents[1] = v["halfExtents"][1].get<float>();
                vol.halfExtents[2] = v["halfExtents"][2].get<float>();
            }
            vol.radius = v.value("radius", 2.f);
            vol.actionId = v.value("actionId", "");
            out.push_back(vol);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteVolumesJson(const std::string& path, const std::vector<GameplayVolume>& volumes) {
    nlohmann::json j;
    j["volumes"] = nlohmann::json::array();
    for (const auto& vol : volumes) {
        nlohmann::json v;
        v["type"] = TypeToString(vol.type);
        v["shape"] = ShapeToString(vol.shape);
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
