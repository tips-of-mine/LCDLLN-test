/**
 * @file ThreatTable.cpp
 * @brief Threat table implementation (M14.2).
 */

#include "engine/ai/ThreatTable.h"

namespace engine::ai {

void ThreatTable::AddThreat(uint32_t mobEntityId, uint32_t sourceEntityId, uint32_t amount) {
    m_mobThreat[mobEntityId].sourceToThreat[sourceEntityId] += amount;
}

uint32_t ThreatTable::GetTarget(uint32_t mobEntityId) const {
    auto it = m_mobThreat.find(mobEntityId);
    if (it == m_mobThreat.end() || it->second.sourceToThreat.empty())
        return 0xFFFFFFFFu;
    uint32_t bestId = 0xFFFFFFFFu;
    uint32_t bestThreat = 0u;
    for (const auto& p : it->second.sourceToThreat) {
        if (p.second > bestThreat) {
            bestThreat = p.second;
            bestId = p.first;
        }
    }
    return bestId;
}

void ThreatTable::Clear(uint32_t mobEntityId) {
    auto it = m_mobThreat.find(mobEntityId);
    if (it != m_mobThreat.end())
        it->second.sourceToThreat.clear();
}

uint32_t ThreatTable::GetThreat(uint32_t mobEntityId, uint32_t sourceEntityId) const {
    auto it = m_mobThreat.find(mobEntityId);
    if (it == m_mobThreat.end()) return 0u;
    auto jt = it->second.sourceToThreat.find(sourceEntityId);
    if (jt == it->second.sourceToThreat.end()) return 0u;
    return jt->second;
}

} // namespace engine::ai
