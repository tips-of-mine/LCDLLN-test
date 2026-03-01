#pragma once

/**
 * @file ThreatTable.h
 * @brief Per-mob threat table: threat += damage; target = max threat (M14.2).
 */

#include <cstdint>
#include <unordered_map>

namespace engine::ai {

/**
 * @brief Threat table for one or more mobs. Add threat on damage; target = entity with max threat.
 */
class ThreatTable {
public:
    ThreatTable() = default;

    /** @brief Adds threat from sourceEntity toward mobEntity (e.g. when source deals damage to mob). */
    void AddThreat(uint32_t mobEntityId, uint32_t sourceEntityId, uint32_t amount);

    /** @brief Returns the entity id with highest threat for this mob, or invalid if table empty. */
    uint32_t GetTarget(uint32_t mobEntityId) const;

    /** @brief Clears all threat entries for the mob (e.g. on return to spawn). */
    void Clear(uint32_t mobEntityId);

    /** @brief Returns current threat value for a source toward a mob (0 if not present). */
    uint32_t GetThreat(uint32_t mobEntityId, uint32_t sourceEntityId) const;

private:
    struct MobThreat {
        std::unordered_map<uint32_t, uint32_t> sourceToThreat;
    };
    std::unordered_map<uint32_t, MobThreat> m_mobThreat;
};

} // namespace engine::ai
