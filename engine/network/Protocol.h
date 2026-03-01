#pragma once

/**
 * @file Protocol.h
 * @brief Minimal protocol: message types and packet layout (M13.1). Separate from game data/serialization.
 */

#include <cstddef>
#include <cstdint>

namespace engine::network {

/** @brief Message type (first byte). Connect, ConnectAck, Snapshot (M13.1). Spawn, Despawn (M13.3). ClientInput: client->server position (M13.3). */
enum class MsgType : uint8_t {
    Connect = 0,
    ConnectAck = 1,
    Snapshot = 2,
    Spawn = 3,
    Despawn = 4,
    ClientInput = 5,
};

constexpr size_t kMaxSnapshotPayload = 0u;
constexpr uint16_t kDefaultServerPort = 27999u;

} // namespace engine::network
