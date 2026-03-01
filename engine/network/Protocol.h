#pragma once

/**
 * @file Protocol.h
 * @brief Minimal protocol: message types and packet layout (M13.1). Separate from game data/serialization.
 */

#include <cstddef>
#include <cstdint>

namespace engine::network {

/** @brief Message type (first byte). Connect, ConnectAck, Snapshot (M13.1). Spawn, Despawn (M13.3). ClientInput (M13.3). ZoneChange (M13.4). AttackRequest/CombatEvent (M14.1). PickupRequest client->server, InventoryDelta server->client (M14.3). */
enum class MsgType : uint8_t {
    Connect = 0,
    ConnectAck = 1,
    Snapshot = 2,
    Spawn = 3,
    Despawn = 4,
    ClientInput = 5,
    ZoneChange = 6,
    AttackRequest = 7,
    CombatEvent = 8,
    PickupRequest = 9,
    InventoryDelta = 10,
    Logout = 11,
    QuestDelta = 12,
    AcceptQuest = 13,
};

constexpr size_t kMaxSnapshotPayload = 0u;
constexpr uint16_t kDefaultServerPort = 27999u;

} // namespace engine::network
