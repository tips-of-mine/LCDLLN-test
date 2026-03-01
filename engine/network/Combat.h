#pragma once

/**
 * @file Combat.h
 * @brief AttackRequest (client->server) and CombatEvent (server->client) (M14.1).
 */

#include "engine/network/Protocol.h"
#include <cstdint>
#include <vector>

namespace engine::network {

/** @brief Serializes an AttackRequest message (attackerId + targetId). Returns bytes written. */
size_t SerializeAttackRequest(uint64_t attackerId, uint64_t targetId, std::vector<uint8_t>& outBuffer);
/** @brief Parses AttackRequest payload (type byte already consumed). Returns true if size >= 16. */
bool ParseAttackRequest(const uint8_t* data, size_t size, uint64_t& outAttackerId, uint64_t& outTargetId);

/** @brief Serializes a CombatEvent message. Returns bytes written. */
size_t SerializeCombatEvent(uint64_t attackerId, uint64_t targetId, uint32_t damage,
    uint32_t targetHpRemaining, bool targetIsDead, std::vector<uint8_t>& outBuffer);
/** @brief Parses CombatEvent payload (type byte already consumed). Returns true if size >= 25. */
bool ParseCombatEvent(const uint8_t* data, size_t size,
    uint64_t& outAttackerId, uint64_t& outTargetId, uint32_t& outDamage,
    uint32_t& outTargetHpRemaining, bool& outTargetIsDead);

} // namespace engine::network
