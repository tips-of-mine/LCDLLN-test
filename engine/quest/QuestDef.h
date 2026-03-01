#pragma once

/**
 * @file QuestDef.h
 * @brief Quest definitions: JSON schema and data structures (M15.1).
 */

#include <cstdint>
#include <string>
#include <vector>

namespace engine::quest {

/** @brief Step type: kill, collect, talk, enter (trigger). */
enum class QuestStepType : uint8_t {
    Kill = 0,
    Collect = 1,
    Talk = 2,
    Enter = 3,
};

/** @brief One step in a quest (e.g. kill N of target, collect itemId, talk to actionId, enter trigger). */
struct QuestStepDef {
    QuestStepType type = QuestStepType::Kill;
    std::string target;
    uint32_t count = 1u;
};

/** @brief Reward item (itemId + count). */
struct QuestRewardItem {
    uint32_t itemId = 0;
    uint32_t count = 0;
};

/** @brief Rewards for completing a quest. */
struct QuestRewardsDef {
    uint32_t xp = 0;
    uint32_t gold = 0;
    std::vector<QuestRewardItem> items;
};

/** @brief One quest definition (id, title, prereqs, steps, rewards). */
struct QuestDef {
    uint32_t id = 0;
    std::string title;
    std::vector<uint32_t> prereqs;
    std::vector<QuestStepDef> steps;
    QuestRewardsDef rewards;
};

/** @brief Loads quest definitions from JSON file. Path is content-relative resolved by caller. */
bool LoadQuestsJson(const std::string& path, std::vector<QuestDef>& out);

} // namespace engine::quest
