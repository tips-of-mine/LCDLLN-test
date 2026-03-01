#pragma once

/**
 * @file EventDef.h
 * @brief Dynamic event definitions: JSON schema, phases, rewards (M15.3).
 */

#include <cstdint>
#include <string>
#include <vector>

namespace engine::event {

/** @brief Trigger type: time = periodic; random = chance per tick. */
enum class EventTriggerType : uint8_t {
    Time = 0,
    Random = 1,
};

/** @brief One spawn entry for a phase (spawner-like: position + archetype + count). */
struct EventPhaseSpawn {
    float position[3] = {0.f, 0.f, 0.f};
    uint32_t archetypeId = 1u;
    uint32_t count = 1u;
};

/** @brief One phase (duration, label, optional spawn wave). */
struct EventPhaseDef {
    float durationSec = 30.f;
    std::string label;
    std::vector<EventPhaseSpawn> spawns;
};

/** @brief Reward item (itemId + count). */
struct EventRewardItem {
    uint32_t itemId = 0;
    uint32_t count = 0;
};

/** @brief Event definition: trigger, phases, rewards, cooldown. */
struct EventDef {
    uint32_t id = 0;
    int32_t zoneId = 0;
    EventTriggerType triggerType = EventTriggerType::Time;
    float triggerIntervalSec = 60.f;
    float triggerChancePerTick = 0.01f;
    std::vector<EventPhaseDef> phases;
    std::vector<EventRewardItem> rewards;
    float cooldownSec = 300.f;
};

/** @brief Loads event definitions from JSON. Path is content-relative resolved by caller. */
bool LoadEventsJson(const std::string& path, std::vector<EventDef>& out);

} // namespace engine::event
