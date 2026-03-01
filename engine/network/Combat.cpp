/**
 * @file Combat.cpp
 * @brief AttackRequest / CombatEvent serialization (M14.1).
 */

#include "engine/network/Combat.h"

#include <cstring>

namespace engine::network {

namespace {
constexpr size_t kAttackRequestPayload = 8 + 8;
constexpr size_t kCombatEventPayload = 8 + 8 + 4 + 4 + 1;
} // namespace

size_t SerializeAttackRequest(uint64_t attackerId, uint64_t targetId, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1 + kAttackRequestPayload);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::AttackRequest);
    std::memcpy(p + 1, &attackerId, 8);
    std::memcpy(p + 9, &targetId, 8);
    return 1 + kAttackRequestPayload;
}

bool ParseAttackRequest(const uint8_t* data, size_t size, uint64_t& outAttackerId, uint64_t& outTargetId) {
    if (size < kAttackRequestPayload) return false;
    std::memcpy(&outAttackerId, data, 8);
    std::memcpy(&outTargetId, data + 8, 8);
    return true;
}

size_t SerializeCombatEvent(uint64_t attackerId, uint64_t targetId, uint32_t damage,
                            uint32_t targetHpRemaining, bool targetIsDead, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1 + kCombatEventPayload);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::CombatEvent);
    std::memcpy(p + 1, &attackerId, 8);
    std::memcpy(p + 9, &targetId, 8);
    std::memcpy(p + 17, &damage, 4);
    std::memcpy(p + 21, &targetHpRemaining, 4);
    p[25] = targetIsDead ? 1u : 0u;
    return 1 + kCombatEventPayload;
}

bool ParseCombatEvent(const uint8_t* data, size_t size,
                      uint64_t& outAttackerId, uint64_t& outTargetId, uint32_t& outDamage,
                      uint32_t& outTargetHpRemaining, bool& outTargetIsDead) {
    if (size < kCombatEventPayload) return false;
    std::memcpy(&outAttackerId, data, 8);
    std::memcpy(&outTargetId, data + 8, 8);
    std::memcpy(&outDamage, data + 16, 4);
    std::memcpy(&outTargetHpRemaining, data + 20, 4);
    outTargetIsDead = (data[24] != 0);
    return true;
}

} // namespace engine::network
