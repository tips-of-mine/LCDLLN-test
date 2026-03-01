#pragma once

/**
 * @file Protocol.h
 * @brief Minimal protocol: message types and packet layout (M13.1). Separate from game data/serialization.
 */

#include <cstddef>
#include <cstdint>

namespace engine::network {

/** @brief Message type (first byte of packet). Connect: client->server, no payload. ConnectAck: server->client, payload clientId. Snapshot: server->client, payload tick then empty. */
enum class MsgType : uint8_t {
    Connect = 0,
    ConnectAck = 1,
    Snapshot = 2,
};

constexpr size_t kMaxSnapshotPayload = 0u;
constexpr uint16_t kDefaultServerPort = 27999u;

} // namespace engine::network
