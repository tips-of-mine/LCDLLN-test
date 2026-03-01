#pragma once

/** @file ServerCore.h — Server tick, connections, empty snapshot (M13.1). */

#include "engine/network/Protocol.h"
#include "engine/network/UdpSocket.h"
#include <cstdint>
#include <vector>

namespace engine::network {

/** @brief One connected client (address + assigned id + persistent character id). */
struct ServerClient {
    PeerAddress address{};
    uint32_t clientId = 0;
    int64_t characterId = 0;
};

/** @brief One tick: drain incoming (handshake), build empty snapshot, send to all clients. */
void ServerTick(int fd, std::vector<ServerClient>& clients, uint32_t& nextId, uint32_t& tick);

constexpr double kServerTickInterval = 1.0 / 20.0;

} // namespace engine::network
