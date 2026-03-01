/**
 * @file UIModel.cpp
 * @brief UIModel: Apply handlers and observer notification (M16.1).
 */

#include "engine/ui/UIModel.h"
#include <sstream>

namespace engine::ui {

void UIModel::ApplyCombatEvent(uint32_t /*attackerId*/, uint32_t targetId, uint32_t /*damage*/,
    uint32_t targetHpRemaining, bool /*targetDead*/) {
    if (targetId != m_clientId) return;
    m_playerStats.hp = targetHpRemaining;
    NotifyObservers();
}

void UIModel::ApplyInventoryDelta(const uint32_t* itemIds, const uint32_t* counts, size_t numEntries) {
    for (size_t i = 0; i < numEntries; ++i)
        m_inventory[itemIds[i]] += counts[i];
    NotifyObservers();
}

void UIModel::ApplyQuestDelta(uint32_t questId, uint32_t stepIndex, uint32_t counter, bool completed) {
    QuestEntry& e = m_quests[questId];
    e.stepIndex = stepIndex;
    e.counter = counter;
    e.completed = completed;
    NotifyObservers();
}

void UIModel::ApplyEventState(uint32_t eventId, uint8_t state, uint32_t phaseIndex, uint32_t phaseCount,
    const std::string& text) {
    m_lastEvent.eventId = eventId;
    m_lastEvent.state = state;
    m_lastEvent.phaseIndex = phaseIndex;
    m_lastEvent.phaseCount = phaseCount;
    m_lastEvent.text = text;
    NotifyObservers();
}

void UIModel::ApplyConnectAck(int32_t zoneId, float posX, float posY, float posZ) {
    m_playerStats.zoneId = zoneId;
    m_playerStats.position[0] = posX;
    m_playerStats.position[1] = posY;
    m_playerStats.position[2] = posZ;
    NotifyObservers();
}

void UIModel::ApplySnapshot(uint32_t ourEntityId, const float* positions, const uint32_t* entityIds, size_t numEntities) {
    for (size_t i = 0; i < numEntities; ++i) {
        if (entityIds[i] == ourEntityId) {
            m_playerStats.position[0] = positions[i * 3 + 0];
            m_playerStats.position[1] = positions[i * 3 + 1];
            m_playerStats.position[2] = positions[i * 3 + 2];
            NotifyObservers();
            return;
        }
    }
}

size_t UIModel::RegisterObserver(std::function<void()> callback) {
    size_t id = ++m_nextObserverId;
    m_observers[id] = std::move(callback);
    return id;
}

void UIModel::UnregisterObserver(size_t id) {
    m_observers.erase(id);
}

void UIModel::DumpToDebug(std::string& out) const {
    std::ostringstream os;
    os << "UIModel: hp=" << m_playerStats.hp << "/" << m_playerStats.maxHp
       << " zone=" << m_playerStats.zoneId
       << " pos=(" << m_playerStats.position[0] << "," << m_playerStats.position[1] << "," << m_playerStats.position[2] << ")";
    os << " inv=[";
    for (const auto& p : m_inventory) {
        if (p.second != 0u) os << "item" << p.first << "=" << p.second << " ";
    }
    os << "] quests=" << m_quests.size();
    os << " event=" << m_lastEvent.eventId << " " << static_cast<int>(m_lastEvent.state)
       << " " << m_lastEvent.phaseIndex << "/" << m_lastEvent.phaseCount << " \"" << m_lastEvent.text << "\"";
    out = os.str();
}

void UIModel::NotifyObservers() {
    for (const auto& p : m_observers) {
        if (p.second) p.second();
    }
}

} // namespace engine::ui
