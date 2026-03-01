/**
 * @file main_server.cpp
 * @brief Server app: UDP bind, tick 20 Hz, Connect/ClientInput, replication by interest (Spawn/Despawn/Snapshot) (M13.1, M13.3).
 */

#include "engine/network/Protocol.h"
#include "engine/network/Replication.h"
#include "engine/network/ServerCore.h"
#include "engine/network/UdpSocket.h"
#include "engine/world/CellGrid.h"
#include "engine/world/InterestSet.h"

#include <array>
#include <chrono>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
bool PeerAddressEqual(const engine::network::PeerAddress& a, const engine::network::PeerAddress& b) {
    return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
}
} // namespace

int main(int, char**) {
    if (!engine::network::NetworkInit()) return 1;
    int fd = engine::network::UdpSocketBind(engine::network::kDefaultServerPort);
    if (fd < 0) {
        engine::network::NetworkShutdown();
        return 1;
    }
    engine::network::UdpSetNonBlocking(fd);

    std::vector<engine::network::ServerClient> clients;
    std::vector<std::array<float, 3>> clientPositions;
    std::vector<engine::world::ClientInterestSet> clientInterestSets;
    std::unordered_map<uint32_t, engine::network::ReplicationEntityState> entityStates;
    uint32_t nextId = 0;
    uint32_t tick = 0;
    constexpr size_t kRecvBuf = 256;
    uint8_t buf[kRecvBuf];
    engine::network::PeerAddress from{};

    auto nextTick = std::chrono::steady_clock::now();
    const auto tickDur = std::chrono::duration<double>(engine::network::kServerTickInterval);

    for (;;) {
        auto now = std::chrono::steady_clock::now();
        while (now >= nextTick) {
            while (engine::network::UdpRecvFrom(fd, buf, kRecvBuf, &from) > 0) {
                if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::Connect)) {
                    engine::network::ServerClient c;
                    c.address = from;
                    c.clientId = nextId++;
                    clients.push_back(c);
                    clientPositions.push_back({0.f, 0.f, 0.f});
                    clientInterestSets.push_back(engine::world::ClientInterestSet{});
                    engine::network::ReplicationEntityState st{};
                    st.entityId = c.clientId;
                    st.archetypeId = 0;
                    entityStates[c.clientId] = st;

                    uint8_t ack[5];
                    ack[0] = static_cast<uint8_t>(engine::network::MsgType::ConnectAck);
                    std::memcpy(ack + 1, &c.clientId, 4);
                    engine::network::UdpSendTo(fd, ack, sizeof(ack), &from);
                } else if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::ClientInput) && kRecvBuf >= 13) {
                    float pos[3];
                    std::memcpy(pos, buf + 1, 12);
                    for (size_t i = 0; i < clients.size(); ++i) {
                        if (!PeerAddressEqual(clients[i].address, from)) continue;
                        clientPositions[i][0] = pos[0];
                        clientPositions[i][1] = pos[1];
                        clientPositions[i][2] = pos[2];
                        auto it = entityStates.find(clients[i].clientId);
                        if (it != entityStates.end()) {
                            it->second.position[0] = pos[0];
                            it->second.position[1] = pos[1];
                            it->second.position[2] = pos[2];
                        }
                        break;
                    }
                }
            }

            engine::world::CellGrid cellGrid(0, 0);
            for (size_t i = 0; i < clients.size(); ++i)
                cellGrid.Insert(static_cast<uint32_t>(clients[i].clientId), clientPositions[i][0], clientPositions[i][2]);

            for (size_t i = 0; i < clients.size(); ++i)
                clientInterestSets[i].Update(clientPositions[i][0], clientPositions[i][2]);

            auto toCellArray = [](const std::vector<engine::world::CellCoord>& cells) {
                std::vector<std::array<int32_t, 4>> out;
                for (const auto& c : cells)
                    out.push_back({{c.zoneX, c.zoneZ, c.cellX, c.cellZ}});
                return out;
            };

            for (size_t i = 0; i < clients.size(); ++i) {
                std::vector<std::vector<uint8_t>> spawns, despawns;
                std::vector<uint8_t> snapshot;
                engine::network::BuildReplicationForClient(
                    tick,
                    toCellArray(clientInterestSets[i].CellsEntered()),
                    toCellArray(clientInterestSets[i].CellsLeft()),
                    toCellArray(clientInterestSets[i].CurrentCells()),
                    [&cellGrid](int32_t zx, int32_t zz, int32_t cx, int32_t cz) {
                        std::vector<uint32_t> u32 = cellGrid.GetEntityIdsInCell(cx, cz);
                        std::vector<uint64_t> u64(u32.begin(), u32.end());
                        return u64;
                    },
                    [&entityStates](uint64_t eid, engine::network::ReplicationEntityState& out) {
                        auto it = entityStates.find(static_cast<uint32_t>(eid));
                        if (it == entityStates.end()) return false;
                        out = it->second;
                        return true;
                    },
                    spawns, despawns, snapshot);

                for (const auto& pkt : spawns)
                    engine::network::UdpSendTo(fd, pkt.data(), pkt.size(), &clients[i].address);
                for (const auto& pkt : despawns)
                    engine::network::UdpSendTo(fd, pkt.data(), pkt.size(), &clients[i].address);
                if (!snapshot.empty())
                    engine::network::UdpSendTo(fd, snapshot.data(), snapshot.size(), &clients[i].address);
            }
            tick++;
            nextTick += tickDur;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
