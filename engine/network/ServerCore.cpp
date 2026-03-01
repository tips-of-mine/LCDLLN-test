/**
 * @file ServerCore.cpp
 * @brief Server tick: accept handshake, build empty snapshot, send (M13.1).
 */

#include "engine/network/ServerCore.h"
#include <cstring>

namespace engine::network {

void ServerTick(int fd, std::vector<ServerClient>& clients, uint32_t& nextId, uint32_t& tick) {
    uint8_t buf[64];
    PeerAddress from{};
    while (UdpRecvFrom(fd, buf, sizeof(buf), &from) > 0) {
        if (buf[0] != static_cast<uint8_t>(MsgType::Connect)) continue;
        ServerClient c;
        c.address = from;
        c.clientId = nextId++;
        clients.push_back(c);
        uint8_t ack[5];
        ack[0] = static_cast<uint8_t>(MsgType::ConnectAck);
        std::memcpy(ack + 1, &c.clientId, 4);
        UdpSendTo(fd, ack, sizeof(ack), &from);
    }

    uint8_t snap[5];
    snap[0] = static_cast<uint8_t>(MsgType::Snapshot);
    std::memcpy(snap + 1, &tick, 4);
    for (const auto& cl : clients)
        UdpSendTo(fd, snap, sizeof(snap), &cl.address);
    tick++;
}

} // namespace engine::network
