/**
 * @file main_server.cpp
 * @brief Server app: UDP bind, tick 20 Hz, Connect/ClientInput, replication, zone transitions, combat, mob AI (M13.1, M13.3, M13.4, M14.1, M14.2).
 */

#include "engine/ai/AiState.h"
#include "engine/ai/MobAiUpdate.h"
#include "engine/ai/ThreatTable.h"
#include "engine/network/Combat.h"
#include "engine/network/Protocol.h"
#include "engine/network/Replication.h"
#include "engine/network/ServerCore.h"
#include "engine/network/UdpSocket.h"
#include "engine/world/CellGrid.h"
#include "engine/world/GameplayVolume.h"
#include "engine/world/InterestSet.h"
#include "engine/world/VolumeFormat.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
bool PeerAddressEqual(const engine::network::PeerAddress& a, const engine::network::PeerAddress& b) {
    return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
}
int32_t ParseZoneIdFromActionId(const std::string& actionId) {
    if (actionId.empty()) return 0;
    try {
        return static_cast<int32_t>(std::stoi(actionId));
    } catch (...) {}
    if (actionId.size() > 6 && actionId.compare(0, 6, "zone_") == 0) {
        try {
            return static_cast<int32_t>(std::stoi(actionId.substr(6)));
        } catch (...) {}
    }
    return 0;
}

constexpr float kAttackRange = 5.0f;
constexpr uint32_t kAttackCooldownTicks = 20u;
constexpr uint32_t kAttackDamage = 10u;
constexpr uint32_t kDefaultMaxHp = 100u;
constexpr uint32_t kFirstMobEntityId = 1000u;

struct EntityCombat {
    uint32_t hp = kDefaultMaxHp;
    uint32_t maxHp = kDefaultMaxHp;
    uint32_t lastAttackTick = 0u;
    bool isDead = false;
};

struct Mob {
    uint32_t entityId = 0;
    int32_t zoneId = 0;
    float position[3] = {0.f, 0.f, 0.f};
    float spawnPosition[3] = {0.f, 0.f, 0.f};
    engine::ai::MobAiState aiState = engine::ai::MobAiState::Idle;
};
} // namespace

int main(int argc, char** argv) {
    if (!engine::network::NetworkInit()) return 1;
    int fd = engine::network::UdpSocketBind(engine::network::kDefaultServerPort);
    if (fd < 0) {
        engine::network::NetworkShutdown();
        return 1;
    }
    engine::network::UdpSetNonBlocking(fd);

    const char* contentPath = (argc >= 2) ? argv[1] : "game/data";
    std::map<int32_t, std::vector<engine::world::GameplayVolume>> volumesPerZone;
    for (int32_t z = 0; z <= 1; ++z) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/zones/zone_%03d/volumes.json", contentPath, static_cast<int>(z));
        std::vector<engine::world::GameplayVolume> vols;
        if (engine::world::ReadVolumesJson(path, vols))
            volumesPerZone[z] = std::move(vols);
    }

    std::vector<engine::network::ServerClient> clients;
    std::vector<std::array<float, 3>> clientPositions;
    std::vector<int32_t> clientZoneIds;
    std::vector<engine::world::ClientInterestSet> clientInterestSets;
    std::unordered_map<uint32_t, engine::network::ReplicationEntityState> entityStates;
    std::unordered_map<uint32_t, EntityCombat> entityCombat;
    std::vector<Mob> mobs;
    engine::ai::ThreatTable threatTable;
    const float leashDistance = engine::ai::kDefaultLeashDistance;
    {
        Mob m;
        m.entityId = kFirstMobEntityId;
        m.zoneId = 0;
        m.position[0] = 50.f;
        m.position[1] = 0.f;
        m.position[2] = 50.f;
        m.spawnPosition[0] = 50.f;
        m.spawnPosition[1] = 0.f;
        m.spawnPosition[2] = 50.f;
        mobs.push_back(m);
        engine::network::ReplicationEntityState st{};
        st.entityId = m.entityId;
        st.archetypeId = 1;
        st.position[0] = m.position[0];
        st.position[1] = m.position[1];
        st.position[2] = m.position[2];
        entityStates[m.entityId] = st;
        entityCombat[m.entityId] = EntityCombat{};
    }
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
                    clientZoneIds.push_back(0);
                    clientInterestSets.push_back(engine::world::ClientInterestSet{});
                    engine::network::ReplicationEntityState st{};
                    st.entityId = c.clientId;
                    st.archetypeId = 0;
                    entityStates[c.clientId] = st;
                    entityCombat[c.clientId] = EntityCombat{};

                    uint8_t ack[5];
                    ack[0] = static_cast<uint8_t>(engine::network::MsgType::ConnectAck);
                    std::memcpy(ack + 1, &c.clientId, 4);
                    engine::network::UdpSendTo(fd, ack, sizeof(ack), &from);
                } else if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::AttackRequest) && kRecvBuf >= 17) {
                    uint64_t attackerId = 0, targetId = 0;
                    if (engine::network::ParseAttackRequest(buf + 1, 16u, attackerId, targetId)) {
                        uint32_t aId = static_cast<uint32_t>(attackerId);
                        uint32_t tId = static_cast<uint32_t>(targetId);
                        bool valid = entityCombat.count(aId) && entityCombat.count(tId) && !entityCombat[tId].isDead;
                        int32_t attackerZone = -1;
                        float ax = 0.f, ay = 0.f, az = 0.f;
                        for (size_t i = 0; valid && i < clients.size(); ++i) {
                            if (clients[i].clientId == aId) {
                                attackerZone = clientZoneIds[i];
                                ax = clientPositions[i][0]; ay = clientPositions[i][1]; az = clientPositions[i][2];
                                break;
                            }
                        }
                        if (valid && attackerZone >= 0) {
                            float tx = entityStates[tId].position[0], tz = entityStates[tId].position[2];
                            float dx = tx - ax, dz = tz - az;
                            if (std::sqrt(dx * dx + dz * dz) > kAttackRange) valid = false;
                            int32_t targetZone = -1;
                            for (size_t i = 0; valid && i < clients.size(); ++i)
                                if (clients[i].clientId == tId) { targetZone = clientZoneIds[i]; break; }
                            if (valid && targetZone < 0)
                                for (const auto& m : mobs)
                                    if (m.entityId == tId) { targetZone = m.zoneId; break; }
                                if (valid && targetZone == attackerZone &&
                                    entityCombat[aId].lastAttackTick + kAttackCooldownTicks <= tick) {
                                    EntityCombat& tc = entityCombat[tId];
                                    uint32_t dmg = (tc.hp >= kAttackDamage) ? kAttackDamage : tc.hp;
                                    tc.hp -= dmg;
                                    if (tc.hp == 0u) tc.isDead = true;
                                    entityCombat[aId].lastAttackTick = tick;
                                    threatTable.AddThreat(tId, aId, dmg);
                                std::vector<uint8_t> ev;
                                engine::network::SerializeCombatEvent(aId, tId, dmg, tc.hp, tc.isDead, ev);
                                for (size_t i = 0; i < clients.size(); ++i)
                                    engine::network::UdpSendTo(fd, ev.data(), ev.size(), &clients[i].address);
                            }
                        }
                    }
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

            for (size_t i = 0; i < clients.size(); ++i) {
                auto it = volumesPerZone.find(clientZoneIds[i]);
                if (it != volumesPerZone.end()) {
                    for (const auto& vol : it->second) {
                        if (vol.type != engine::world::VolumeType::ZoneTransition) continue;
                        if (!engine::world::PointInVolume(clientPositions[i][0], clientPositions[i][1], clientPositions[i][2], vol))
                            continue;
                        int32_t targetZone = ParseZoneIdFromActionId(vol.actionId);
                        float spawnPos[3] = { vol.position[0], vol.position[1], vol.position[2] };
                        std::vector<uint8_t> zc;
                        engine::network::SerializeZoneChange(targetZone, spawnPos, zc);
                        engine::network::UdpSendTo(fd, zc.data(), zc.size(), &clients[i].address);
                        clientZoneIds[i] = targetZone;
                        clientPositions[i][0] = spawnPos[0];
                        clientPositions[i][1] = spawnPos[1];
                        clientPositions[i][2] = spawnPos[2];
                        auto es = entityStates.find(clients[i].clientId);
                        if (es != entityStates.end()) {
                            es->second.position[0] = spawnPos[0];
                            es->second.position[1] = spawnPos[1];
                            es->second.position[2] = spawnPos[2];
                        }
                        break;
                    }
                }
            }

            if (tick % engine::ai::kAiUpdateTickStep == 0u) {
                const float aiDt = engine::ai::kAiUpdateTickStep * static_cast<float>(engine::network::kServerTickInterval);
                auto getEntityPos = [&](uint32_t entityId, float& outX, float& outZ) -> bool {
                    for (size_t i = 0; i < clients.size(); ++i) {
                        if (clients[i].clientId == entityId) {
                            outX = clientPositions[i][0];
                            outZ = clientPositions[i][2];
                            return true;
                        }
                    }
                    auto it = entityStates.find(entityId);
                    if (it != entityStates.end()) {
                        outX = it->second.position[0];
                        outZ = it->second.position[2];
                        return true;
                    }
                    return false;
                };
                for (Mob& m : mobs) {
                    if (entityCombat[m.entityId].isDead) continue;
                    auto it = entityStates.find(m.entityId);
                    if (it == entityStates.end()) continue;
                    float curPos[3] = { it->second.position[0], it->second.position[1], it->second.position[2] };
                    engine::ai::UpdateMobAi(m.entityId, m.aiState, m.spawnPosition, curPos, threatTable,
                        leashDistance, getEntityPos, engine::ai::kDefaultMobMoveSpeed, aiDt);
                    it->second.position[0] = curPos[0];
                    it->second.position[1] = curPos[1];
                    it->second.position[2] = curPos[2];
                    m.position[0] = curPos[0];
                    m.position[1] = curPos[1];
                    m.position[2] = curPos[2];

                    if (m.aiState == engine::ai::MobAiState::Aggro) {
                        uint32_t targetId = threatTable.GetTarget(m.entityId);
                        if (targetId != 0xFFFFFFFFu && entityCombat.count(targetId) && !entityCombat[targetId].isDead) {
                            float tx = 0.f, tz = 0.f;
                            if (getEntityPos(targetId, tx, tz)) {
                                float dx = tx - curPos[0], dz = tz - curPos[2];
                                if (std::sqrt(dx * dx + dz * dz) <= kAttackRange &&
                                    entityCombat[m.entityId].lastAttackTick + kAttackCooldownTicks <= tick) {
                                    EntityCombat& tc = entityCombat[targetId];
                                    uint32_t dmg = (tc.hp >= kAttackDamage) ? kAttackDamage : tc.hp;
                                    tc.hp -= dmg;
                                    if (tc.hp == 0u) tc.isDead = true;
                                    entityCombat[m.entityId].lastAttackTick = tick;
                                    std::vector<uint8_t> ev;
                                    engine::network::SerializeCombatEvent(m.entityId, targetId, dmg, tc.hp, tc.isDead, ev);
                                    for (size_t i = 0; i < clients.size(); ++i)
                                        engine::network::UdpSendTo(fd, ev.data(), ev.size(), &clients[i].address);
                                }
                            }
                        }
                    }
                }
            }

            std::map<int32_t, engine::world::CellGrid> zoneGrids;
            for (size_t i = 0; i < clients.size(); ++i) {
                int32_t zid = clientZoneIds[i];
                if (zoneGrids.find(zid) == zoneGrids.end())
                    zoneGrids[zid] = engine::world::CellGrid(0, 0);
                zoneGrids[zid].Insert(static_cast<uint32_t>(clients[i].clientId), clientPositions[i][0], clientPositions[i][2]);
            }
            for (const auto& m : mobs) {
                if (zoneGrids.find(m.zoneId) == zoneGrids.end())
                    zoneGrids[m.zoneId] = engine::world::CellGrid(0, 0);
                zoneGrids[m.zoneId].Insert(m.entityId, m.position[0], m.position[2]);
            }

            for (size_t i = 0; i < clients.size(); ++i)
                clientInterestSets[i].Update(clientPositions[i][0], clientPositions[i][2]);

            auto toCellArray = [](const std::vector<engine::world::CellCoord>& cells) {
                std::vector<std::array<int32_t, 4>> out;
                for (const auto& c : cells)
                    out.push_back({{c.zoneX, c.zoneZ, c.cellX, c.cellZ}});
                return out;
            };

            for (size_t i = 0; i < clients.size(); ++i) {
                engine::world::CellGrid& cellGrid = zoneGrids[clientZoneIds[i]];
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
