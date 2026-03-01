/**
 * @file EventDef.cpp
 * @brief Load event definitions from JSON (M15.3).
 */

#include "engine/event/EventDef.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace engine::event {

namespace {
EventTriggerType StringToTriggerType(const std::string& s) {
    if (s == "random") return EventTriggerType::Random;
    return EventTriggerType::Time;
}
} // namespace

bool LoadEventsJson(const std::string& path, std::vector<EventDef>& out) {
    out.clear();
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto arr = j.find("events");
        if (arr == j.end() || !arr->is_array()) return true;
        for (const auto& e : *arr) {
            EventDef def;
            def.id = e.value("id", 0u);
            def.zoneId = e.value("zoneId", 0);
            def.triggerType = StringToTriggerType(e.value("trigger", "time"));
            def.triggerIntervalSec = e.value("triggerIntervalSec", 60.f);
            def.triggerChancePerTick = e.value("triggerChancePerTick", 0.01f);
            def.cooldownSec = e.value("cooldownSec", 300.f);
            if (e.contains("phases") && e["phases"].is_array()) {
                for (const auto& p : e["phases"]) {
                    EventPhaseDef phase;
                    phase.durationSec = p.value("durationSec", 30.f);
                    phase.label = p.value("label", "");
                    if (p.contains("spawns") && p["spawns"].is_array()) {
                        for (const auto& s : p["spawns"]) {
                            EventPhaseSpawn sp;
                            sp.archetypeId = s.value("archetypeId", 1u);
                            sp.count = s.value("count", 1u);
                            if (s.contains("position") && s["position"].is_array() && s["position"].size() >= 3) {
                                sp.position[0] = s["position"][0].get<float>();
                                sp.position[1] = s["position"][1].get<float>();
                                sp.position[2] = s["position"][2].get<float>();
                            }
                            phase.spawns.push_back(sp);
                        }
                    }
                    def.phases.push_back(phase);
                }
            }
            if (e.contains("rewards") && e["rewards"].is_array()) {
                for (const auto& r : e["rewards"]) {
                    EventRewardItem ri;
                    ri.itemId = r.value("itemId", 0u);
                    ri.count = r.value("count", 0u);
                    def.rewards.push_back(ri);
                }
            }
            out.push_back(def);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace engine::event
