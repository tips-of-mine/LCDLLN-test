/**
 * @file QuestDef.cpp
 * @brief Load quest definitions from JSON (M15.1).
 */

#include "engine/quest/QuestDef.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace engine::quest {

namespace {
QuestStepType StringToStepType(const std::string& s) {
    if (s == "collect") return QuestStepType::Collect;
    if (s == "talk") return QuestStepType::Talk;
    if (s == "enter") return QuestStepType::Enter;
    return QuestStepType::Kill;
}
} // namespace

bool LoadQuestsJson(const std::string& path, std::vector<QuestDef>& out) {
    out.clear();
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto arr = j.find("quests");
        if (arr == j.end() || !arr->is_array()) return true;
        for (const auto& q : *arr) {
            QuestDef def;
            def.id = q.value("id", 0u);
            def.title = q.value("title", "");
            if (q.contains("prereqs") && q["prereqs"].is_array())
                for (const auto& p : q["prereqs"]) def.prereqs.push_back(p.get<uint32_t>());
            if (q.contains("steps") && q["steps"].is_array()) {
                for (const auto& s : q["steps"]) {
                    QuestStepDef step;
                    step.type = StringToStepType(s.value("type", "kill"));
                    step.target = s.value("target", "");
                    step.count = s.value("count", 1u);
                    def.steps.push_back(step);
                }
            }
            if (q.contains("rewards") && q["rewards"].is_object()) {
                auto& r = q["rewards"];
                def.rewards.xp = r.value("xp", 0u);
                def.rewards.gold = r.value("gold", 0u);
                if (r.contains("items") && r["items"].is_array())
                    for (const auto& it : r["items"]) {
                        QuestRewardItem ri;
                        ri.itemId = it.value("itemId", 0u);
                        ri.count = it.value("count", 0u);
                        def.rewards.items.push_back(ri);
                    }
            }
            out.push_back(def);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace engine::quest
