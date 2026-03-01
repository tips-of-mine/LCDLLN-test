/**
 * @file FxDef.cpp
 * @brief Load FX definitions and event->FX mapping from fx.json (M17.1).
 */

#include "engine/fx/FxDef.h"
#include "engine/core/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace engine::fx {

namespace {

FxType StringToFxType(const std::string& s) {
    if (s == "decal") return FxType::Decal;
    if (s == "sound") return FxType::Sound;
    return FxType::Particle;
}

} // namespace

bool LoadFxJson(const std::string& path, std::vector<FxDefinition>& outDefs, EventFxMapping& outMapping) {
    outDefs.clear();
    outMapping.clear();
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_INFO(Render, "FxDef: fx.json not found at {}", path);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        if (j.contains("definitions") && j["definitions"].is_array()) {
            for (const auto& def : j["definitions"]) {
                FxDefinition d;
                d.id = def.value("id", "");
                d.type = StringToFxType(def.value("type", "particle"));
                d.asset = def.value("asset", "");
                d.scale = def.value("scale", 1.0);
                d.duration = def.value("duration", 1.0);
                if (!d.id.empty())
                    outDefs.push_back(std::move(d));
            }
        }
        if (j.contains("mapping") && j["mapping"].is_object()) {
            for (auto it = j["mapping"].begin(); it != j["mapping"].end(); ++it) {
                std::map<std::string, std::string> sub;
                if (it.value().is_object()) {
                    for (auto it2 = it.value().begin(); it2 != it.value().end(); ++it2)
                        sub[it2.key()] = it2.value().get<std::string>();
                }
                outMapping[it.key()] = std::move(sub);
            }
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Render, "FxDef: parse error at {}: {}", path, e.what());
        return false;
    }
}

} // namespace engine::fx
