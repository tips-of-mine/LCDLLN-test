#pragma once

/**
 * @file UIModel.h
 * @brief UI Model: player stats, inventory, quest list, events. Main-thread only; binding from network handlers (M16.1).
 */

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

namespace engine::ui {

/** @brief Player stats (HP from CombatEvent when we are target; position/zone from ConnectAck/Snapshot). */
struct PlayerStats {
    uint32_t hp = 100u;
    uint32_t maxHp = 100u;
    float position[3] = {0.f, 0.f, 0.f};
    int32_t zoneId = 0;
};

/** @brief One quest entry (from QuestDelta). */
struct QuestEntry {
    uint32_t stepIndex = 0u;
    uint32_t counter = 0u;
    bool completed = false;
};

/** @brief Last event notification (from EventState). 0=Idle, 1=Active, 2=Completed. */
struct EventEntry {
    uint32_t eventId = 0u;
    uint8_t state = 0u;
    uint32_t phaseIndex = 0u;
    uint32_t phaseCount = 0u;
    std::string text;
};

/**
 * @brief UI model: stats, inventory, quests, events. Updated by message handlers; notifies observers (main thread only).
 */
class UIModel {
public:
    UIModel() = default;

    /** @brief Sets the local client entity id (used to apply CombatEvent to player stats). */
    void SetClientId(uint32_t clientId) { m_clientId = clientId; }

    /** @brief Applies CombatEvent: if target is us, updates player hp. */
    void ApplyCombatEvent(uint32_t attackerId, uint32_t targetId, uint32_t damage, uint32_t targetHpRemaining, bool targetDead);

    /** @brief Applies InventoryDelta: adds each (itemId, count) to inventory. */
    void ApplyInventoryDelta(const uint32_t* itemIds, const uint32_t* counts, size_t numEntries);

    /** @brief Applies QuestDelta: updates quest entry. */
    void ApplyQuestDelta(uint32_t questId, uint32_t stepIndex, uint32_t counter, bool completed);

    /** @brief Applies EventState: updates last event. */
    void ApplyEventState(uint32_t eventId, uint8_t state, uint32_t phaseIndex, uint32_t phaseCount, const std::string& text);

    /** @brief Applies ConnectAck: sets zone and position. */
    void ApplyConnectAck(int32_t zoneId, float posX, float posY, float posZ);

    /** @brief Applies Snapshot: if our entity is in the list, updates position. */
    void ApplySnapshot(uint32_t ourEntityId, const float* positions, const uint32_t* entityIds, size_t numEntities);

    /** @brief Registers an observer callback (invoked after any Apply*). Returns observer id for UnregisterObserver. */
    size_t RegisterObserver(std::function<void()> callback);

    /** @brief Unregisters an observer by id returned from RegisterObserver. */
    void UnregisterObserver(size_t id);

    /** @brief Read-only access to player stats. */
    const PlayerStats& GetPlayerStats() const { return m_playerStats; }
    /** @brief Read-only access to inventory (itemId -> count). */
    const std::unordered_map<uint32_t, uint32_t>& GetInventory() const { return m_inventory; }
    /** @brief Read-only access to quests (questId -> entry). */
    const std::map<uint32_t, QuestEntry>& GetQuests() const { return m_quests; }
    /** @brief Read-only access to last event. */
    const EventEntry& GetLastEvent() const { return m_lastEvent; }

    /** @brief Debug: appends a human-readable dump of the model to out (reuse buffer to avoid allocations). */
    void DumpToDebug(std::string& out) const;

private:
    void NotifyObservers();

    uint32_t m_clientId = 0xFFFFFFFFu;
    PlayerStats m_playerStats;
    std::unordered_map<uint32_t, uint32_t> m_inventory;
    std::map<uint32_t, QuestEntry> m_quests;
    EventEntry m_lastEvent;
    std::map<size_t, std::function<void()>> m_observers;
    size_t m_nextObserverId = 0u;
};

} // namespace engine::ui
