/**
 * @file main_client.cpp
 * @brief Client app: connect, ClientInput, Spawn/Despawn/Snapshot, AttackRequest, CombatEvent, PickupRequest, InventoryDelta (M13.1, M13.3, M14.1, M14.3).
 */

#include "engine/network/Combat.h"
#include "engine/network/EventProtocol.h"
#include "engine/network/LootProtocol.h"
#include "engine/network/Protocol.h"
#include "engine/network/QuestProtocol.h"
#include "engine/network/Replication.h"
#include "engine/network/UdpSocket.h"
#include "engine/ui/UIModel.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

int main(int argc, char** argv) {
    if (!engine::network::NetworkInit())
        return 1;
    const char* host = (argc >= 2) ? argv[1] : "127.0.0.1";
    uint64_t connectCharacterId = 0u;
    if (argc >= 3) {
        try { connectCharacterId = std::stoull(argv[2]); } catch (...) {}
    }
    const uint16_t port = engine::network::kDefaultServerPort;
    int fd = engine::network::UdpSocketCreate();
    if (fd < 0) {
        engine::network::NetworkShutdown();
        return 1;
    }
    engine::network::PeerAddress serverAddr{};
#ifdef _WIN32
    sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(serverAddr.data);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    inet_pton(AF_INET, host, &sa->sin_addr);
#else
    struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(serverAddr.data);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    inet_pton(AF_INET, host, &sa->sin_addr);
#endif
    uint8_t conn[9];
    conn[0] = static_cast<uint8_t>(engine::network::MsgType::Connect);
    std::memcpy(conn + 1, &connectCharacterId, 8);
    if (engine::network::UdpSendTo(fd, conn, sizeof(conn), &serverAddr) < 0) {
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    if (!engine::network::UdpSetNonBlocking(fd)) {
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    uint32_t clientId = 0xFFFFFFFFu;
    int64_t characterId = 0;
    uint32_t lastTick = 0;
    uint32_t snapCount = 0;
    int32_t currentZoneId = 0;
    float myPosition[3] = { 0.f, 0.f, 0.f };
    auto lastStats = std::chrono::steady_clock::now();
    auto lastInput = std::chrono::steady_clock::now();
    uint8_t buf[1024];
    engine::network::PeerAddress from{};
    std::unordered_map<uint64_t, engine::network::ReplicationEntityState> replicatedEntities;
    std::unordered_map<uint64_t, uint32_t> entityHp;
    engine::ui::UIModel uiModel;
    auto lastAttack = std::chrono::steady_clock::now();
    auto lastPickup = std::chrono::steady_clock::now();
    constexpr uint64_t kMobEntityId = 1000u;
    constexpr uint32_t kLootBagArchetypeId = 2u;
    constexpr float kPickupRange = 5.0f;

    for (int wait = 0; wait < 500; ++wait) {
        int n = engine::network::UdpRecvFrom(fd, buf, sizeof(buf), &from);
        if (n >= 29 && buf[0] == static_cast<uint8_t>(engine::network::MsgType::ConnectAck)) {
            std::memcpy(&clientId, buf + 1, 4);
            std::memcpy(&characterId, buf + 5, 8);
            std::memcpy(&currentZoneId, buf + 13, 4);
            std::memcpy(&myPosition[0], buf + 17, 4);
            std::memcpy(&myPosition[1], buf + 21, 4);
            std::memcpy(&myPosition[2], buf + 25, 4);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (clientId == 0xFFFFFFFFu) {
        std::fprintf(stderr, "client: no ConnectAck received\n");
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    uiModel.SetClientId(clientId);
    uiModel.ApplyConnectAck(currentZoneId, myPosition[0], myPosition[1], myPosition[2]);
    std::printf("client: connected clientId=%u characterId=%lld (reconnect with: %s %lld)\n", clientId, static_cast<long long>(characterId), host, static_cast<long long>(characterId));

    std::vector<uint8_t> acceptQuest;
    engine::network::SerializeAcceptQuest(1u, acceptQuest);
    engine::network::UdpSendTo(fd, acceptQuest.data(), acceptQuest.size(), &serverAddr);

    for (;;) {
        int n = engine::network::UdpRecvFrom(fd, buf, sizeof(buf), &from);
        if (n > 0) {
            uint8_t msgType = buf[0];
            if (msgType == static_cast<uint8_t>(engine::network::MsgType::Spawn) && n >= 41) {
                engine::network::ReplicationEntityState st;
                if (engine::network::ParseSpawn(buf + 1, static_cast<size_t>(n) - 1, st)) {
                    replicatedEntities[st.entityId] = st;
                    if (st.archetypeId != kLootBagArchetypeId && entityHp.find(st.entityId) == entityHp.end())
                        entityHp[st.entityId] = 100u;
                }
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::Despawn) && n >= 9) {
                uint64_t eid;
                if (engine::network::ParseDespawn(buf + 1, static_cast<size_t>(n) - 1, eid))
                    replicatedEntities.erase(eid);
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::Snapshot) && n >= 9) {
                uint32_t tickVal;
                std::vector<engine::network::ReplicationEntityState> states;
                if (engine::network::ParseSnapshotWithStates(buf + 1, static_cast<size_t>(n) - 1, tickVal, states)) {
                    snapCount++;
                    lastTick = tickVal;
                    for (const auto& st : states) {
                        replicatedEntities[st.entityId] = st;
                        if (st.archetypeId != kLootBagArchetypeId && entityHp.find(st.entityId) == entityHp.end())
                            entityHp[st.entityId] = 100u;
                    }
                    std::vector<float> positions(states.size() * 3);
                    std::vector<uint32_t> entityIds(states.size());
                    for (size_t i = 0; i < states.size(); ++i) {
                        entityIds[i] = static_cast<uint32_t>(states[i].entityId);
                        positions[i * 3 + 0] = states[i].position[0];
                        positions[i * 3 + 1] = states[i].position[1];
                        positions[i * 3 + 2] = states[i].position[2];
                    }
                    uiModel.ApplySnapshot(clientId, positions.data(), entityIds.data(), states.size());
                }
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::CombatEvent) && n >= 26) {
                uint64_t aId = 0, tId = 0;
                uint32_t damage = 0, targetHp = 0;
                bool targetDead = false;
                if (engine::network::ParseCombatEvent(buf + 1, static_cast<size_t>(n) - 1, aId, tId, damage, targetHp, targetDead)) {
                    entityHp[static_cast<uint64_t>(tId)] = targetHp;
                    uiModel.ApplyCombatEvent(static_cast<uint32_t>(aId), static_cast<uint32_t>(tId), damage, targetHp, targetDead);
                }
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::ZoneChange) && n >= 17) {
                int32_t newZoneId = 0;
                float spawnPos[3] = {0.f, 0.f, 0.f};
                if (engine::network::ParseZoneChange(buf + 1, static_cast<size_t>(n) - 1, newZoneId, spawnPos)) {
                    replicatedEntities.clear();
                    currentZoneId = newZoneId;
                    myPosition[0] = spawnPos[0];
                    myPosition[1] = spawnPos[1];
                    myPosition[2] = spawnPos[2];
                    uiModel.ApplyConnectAck(newZoneId, spawnPos[0], spawnPos[1], spawnPos[2]);
                }
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::InventoryDelta) && n >= 5) {
                std::vector<engine::network::InventoryDeltaEntry> entries;
                if (engine::network::ParseInventoryDelta(buf + 1, static_cast<size_t>(n) - 1, entries)) {
                    std::vector<uint32_t> ids(entries.size()), counts(entries.size());
                    for (size_t i = 0; i < entries.size(); ++i) {
                        ids[i] = entries[i].itemId;
                        counts[i] = entries[i].count;
                    }
                    uiModel.ApplyInventoryDelta(ids.data(), counts.data(), entries.size());
                }
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::QuestDelta) && n >= 14) {
                uint32_t qId = 0, stepIdx = 0, counter = 0;
                bool completed = false;
                if (engine::network::ParseQuestDelta(buf + 1, static_cast<size_t>(n) - 1, qId, stepIdx, counter, completed))
                    uiModel.ApplyQuestDelta(qId, stepIdx, counter, completed);
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::EventState) && n >= 15) {
                uint32_t evId = 0, phaseIdx = 0, phaseCnt = 0;
                engine::network::EventStateEnum evState = engine::network::EventStateEnum::Idle;
                std::string evText;
                if (engine::network::ParseEventState(buf + 1, static_cast<size_t>(n) - 1, evId, evState, phaseIdx, phaseCnt, evText))
                    uiModel.ApplyEventState(evId, static_cast<uint8_t>(evState), phaseIdx, phaseCnt, evText);
            }
        }

        auto now = std::chrono::steady_clock::now();
        const engine::ui::PlayerStats& stats = uiModel.GetPlayerStats();
        myPosition[0] = stats.position[0];
        myPosition[1] = stats.position[1];
        myPosition[2] = stats.position[2];
        if (std::chrono::duration<double>(now - lastInput).count() >= 0.05) {
            uint8_t input[13];
            input[0] = static_cast<uint8_t>(engine::network::MsgType::ClientInput);
            std::memcpy(input + 1, myPosition, 12);
            engine::network::UdpSendTo(fd, input, sizeof(input), &serverAddr);
            lastInput = now;
        }
        if (std::chrono::duration<double>(now - lastAttack).count() >= 1.5) {
            std::vector<uint8_t> ar;
            engine::network::SerializeAttackRequest(static_cast<uint64_t>(clientId), kMobEntityId, ar);
            engine::network::UdpSendTo(fd, ar.data(), ar.size(), &serverAddr);
            lastAttack = now;
        }
        if (std::chrono::duration<double>(now - lastPickup).count() >= 1.0) {
            for (const auto& p : replicatedEntities) {
                if (p.second.archetypeId != kLootBagArchetypeId) continue;
                float dx = p.second.position[0] - myPosition[0], dz = p.second.position[2] - myPosition[2];
                if (std::sqrt(dx * dx + dz * dz) <= kPickupRange) {
                    std::vector<uint8_t> pr;
                    engine::network::SerializePickupRequest(p.first, pr);
                    engine::network::UdpSendTo(fd, pr.data(), pr.size(), &serverAddr);
                    break;
                }
            }
            lastPickup = now;
        }
        if (std::chrono::duration<double>(now - lastStats).count() >= 2.0) {
            uint32_t mobHp = 100u;
            auto it = entityHp.find(kMobEntityId);
            if (it != entityHp.end()) mobHp = it->second;
            std::string dump;
            uiModel.DumpToDebug(dump);
            std::printf("client: %s | tick=%u entities=%zu mob_hp=%u\n", dump.c_str(), lastTick, replicatedEntities.size(), mobHp);
            lastStats = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    engine::network::UdpSocketClose(fd);
    engine::network::NetworkShutdown();
    return 0;
}
