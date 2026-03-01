/**
 * @file SpawnerDef.cpp
 * @brief Load spawner definitions from JSON (M15.2).
 */

#include "engine/spawner/SpawnerDef.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace engine::spawner {

bool LoadSpawnersJson(const std::string& path, std::vector<SpawnerDef>& out) {
    out.clear();
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto arr = j.find("spawners");
        if (arr == j.end() || !arr->is_array()) return true;
        for (const auto& s : *arr) {
            SpawnerDef def;
            def.zoneId = s.value("zoneId", 0);
            if (s.contains("position") && s["position"].is_array() && s["position"].size() >= 3) {
                def.position[0] = s["position"][0].get<float>();
                def.position[1] = s["position"][1].get<float>();
                def.position[2] = s["position"][2].get<float>();
            }
            def.archetypeId = s.value("archetypeId", 1u);
            def.count = s.value("count", 1u);
            def.respawnSec = s.value("respawnSec", 10.f);
            def.activationRadiusCells = s.value("activationRadiusCells", 1);
            out.push_back(def);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace engine::spawner
