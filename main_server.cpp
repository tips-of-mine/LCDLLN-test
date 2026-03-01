/**
 * @file main_server.cpp
 * @brief Server app: UDP bind, tick 20 Hz, Connect/ClientInput, replication, zone transitions, combat, mob AI, loot, quests (M13.1–M14.4, M15.1).
 */

#include "engine/ai/AiState.h"
#include "engine/ai/MobAiUpdate.h"
#include "engine/ai/ThreatTable.h"
#include "engine/loot/LootTable.h"
#include "engine/network/Combat.h"
#include "engine/network/LootProtocol.h"
#include "engine/network/Protocol.h"
#include "engine/network/QuestProtocol.h"
#include "engine/network/Replication.h"
#include "engine/network/ServerCore.h"
#include "engine/network/UdpSocket.h"
#include "engine/persistence/CharacterDb.h"
#include "engine/event/EventDef.h"
#include "engine/network/EventProtocol.h"
#include "engine/quest/QuestDef.h"
#include "engine/spawner/SpawnerDef.h"
#include "engine/world/CellGrid.h"
#include "engine/world/GameplayVolume.h"
#include "engine/world/InterestSet.h"
#include "engine/world/VolumeFormat.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
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
constexpr uint32_t kFirstLootBagEntityId = 2000u;
constexpr uint32_t kLootBagArchetypeId = 2u;
constexpr float kPickupRange = 5.0f;

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
    int32_t spawnerInstanceIndex = -1;
    int32_t spawnerSlot = -1;
};

struct SpawnerInstance {
    engine::spawner::SpawnerDef def;
    std::vector<uint32_t> spawnedIds;
    std::vector<uint32_t> respawnAtTick;
};

struct LootBag {
    uint32_t entityId = 0;
    int32_t zoneId = 0;
    float position[3] = {0.f, 0.f, 0.f};
    std::vector<engine::loot::LootEntry> items;
    uint32_t ownerId = 0;
};

struct QuestProgress {
    uint32_t stepIndex = 0;
    uint32_t counter = 0;
};

struct EventInstanceState {
    uint8_t state = 0;
    uint32_t currentPhase = 0u;
    uint32_t phaseEndTick = 0u;
    uint32_t cooldownEndTick = 0u;
    uint32_t lastTriggerTick = 0u;
    std::vector<uint32_t> participants;
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
    std::string dbPath = std::string(contentPath) + "/characters.db";
    if (!engine::persistence::Open(dbPath) || !engine::persistence::CreateTablesIfNeeded()) {
        std::fprintf(stderr, "server: failed to open or init DB at %s\n", dbPath.c_str());
        engine::network::NetworkShutdown();
        return 1;
    }
    std::vector<engine::quest::QuestDef> questDefs;
    std::map<uint32_t, engine::quest::QuestDef> questDefsById;
    if (engine::quest::LoadQuestsJson(std::string(contentPath) + "/quests.json", questDefs)) {
        for (const auto& q : questDefs) questDefsById[q.id] = q;
    }
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, QuestProgress>> playerQuestProgress;
    std::unordered_map<uint32_t, std::set<uint32_t>> playerCompletedQuests;
    std::unordered_map<uint32_t, std::set<std::string>> clientTriggersInsidePrev;

    std::map<int32_t, std::vector<engine::world::GameplayVolume>> volumesPerZone;
    for (int32_t z = 0; z <= 1; ++z) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/zones/zone_%03d/volumes.json", contentPath, static_cast<int>(z));
        std::vector<engine::world::GameplayVolume> vols;
        if (engine::world::ReadVolumesJson(path, vols))
            volumesPerZone[z] = std::move(vols);
    }

    std::vector<SpawnerInstance> spawnerInstances;
    for (int32_t z = 0; z <= 1; ++z) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/zones/zone_%03d/spawners.json", contentPath, static_cast<int>(z));
        std::vector<engine::spawner::SpawnerDef> defs;
        if (engine::spawner::LoadSpawnersJson(path, defs)) {
            for (auto& d : defs) {
                d.zoneId = z;
                SpawnerInstance si;
                si.def = d;
                si.spawnedIds.resize(static_cast<size_t>(d.count), 0u);
                si.respawnAtTick.resize(static_cast<size_t>(d.count), 0u);
                spawnerInstances.push_back(std::move(si));
            }
        }
    }

    std::vector<engine::event::EventDef> eventDefs;
    std::vector<EventInstanceState> eventInstances;
    for (int32_t z = 0; z <= 1; ++z) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/zones/zone_%03d/events.json", contentPath, static_cast<int>(z));
        std::vector<engine::event::EventDef> defs;
        if (engine::event::LoadEventsJson(path, defs)) {
            for (auto& d : defs) {
                d.zoneId = z;
                eventDefs.push_back(d);
                EventInstanceState inst;
                inst.cooldownEndTick = 0u;
                inst.lastTriggerTick = 0u;
                eventInstances.push_back(inst);
            }
        }
    }

    std::vector<engine::network::ServerClient> clients;
    std::vector<std::array<float, 3>> clientPositions;
    std::vector<int32_t> clientZoneIds;
    std::vector<engine::world::ClientInterestSet> clientInterestSets;
    std::unordered_map<uint32_t, engine::network::ReplicationEntityState> entityStates;
    std::unordered_map<uint32_t, EntityCombat> entityCombat;
    std::vector<Mob> mobs;
    uint32_t nextMobEntityId = kFirstMobEntityId;
    engine::ai::ThreatTable threatTable;
    const float leashDistance = engine::ai::kDefaultLeashDistance;
    engine::loot::LootTableData defaultLootTable;
    defaultLootTable.entries.push_back({1u, 1u});
    std::vector<LootBag> lootBags;
    uint32_t nextLootBagId = kFirstLootBagEntityId;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> playerInventory;
    uint32_t nextId = 0;
    uint32_t tick = 0;
    constexpr size_t kRecvBuf = 256;
    uint8_t buf[kRecvBuf];
    engine::network::PeerAddress from{};

    auto nextTick = std::chrono::steady_clock::now();
    auto lastAutosave = std::chrono::steady_clock::now();
    const auto tickDur = std::chrono::duration<double>(engine::network::kServerTickInterval);
    constexpr double kAutosaveIntervalSec = 30.0;

    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastAutosave).count() >= kAutosaveIntervalSec) {
            for (size_t i = 0; i < clients.size(); ++i) {
                engine::persistence::CharacterState cs{};
                cs.zoneId = clientZoneIds[i];
                cs.posX = clientPositions[i][0];
                cs.posY = clientPositions[i][1];
                cs.posZ = clientPositions[i][2];
                cs.hp = entityCombat[clients[i].clientId].hp;
                cs.maxHp = entityCombat[clients[i].clientId].maxHp;
                engine::persistence::SaveCharacter(clients[i].characterId, cs);
                engine::persistence::SaveInventory(clients[i].characterId, playerInventory[clients[i].clientId]);
            }
            lastAutosave = now;
        }
            while (now >= nextTick) {
            std::map<int32_t, std::set<std::pair<int32_t, int32_t>>> playerCellsPerZone;
            for (size_t i = 0; i < clients.size(); ++i) {
                int32_t zid = clientZoneIds[i];
                engine::world::CellCoord c = engine::world::WorldToCellCoord(clientPositions[i][0], clientPositions[i][2]);
                playerCellsPerZone[zid].insert({c.cellX, c.cellZ});
            }
            for (size_t si = 0; si < spawnerInstances.size(); ++si) {
                SpawnerInstance& inst = spawnerInstances[si];
                const engine::spawner::SpawnerDef& def = inst.def;
                engine::world::CellCoord center = engine::world::WorldToCellCoord(def.position[0], def.position[2]);
                std::vector<engine::world::CellCoord> cellsInRadius;
                engine::world::GetCellsInRadius(center, def.activationRadiusCells, cellsInRadius);
                bool active = false;
                auto it = playerCellsPerZone.find(def.zoneId);
                if (it != playerCellsPerZone.end()) {
                    for (const auto& c : cellsInRadius) {
                        if (it->second.count({c.cellX, c.cellZ})) { active = true; break; }
                    }
                }
                if (active) {
                    uint32_t respawnTicks = static_cast<uint32_t>(def.respawnSec * 20.0f);
                    for (size_t slot = 0; slot < inst.spawnedIds.size(); ++slot) {
                        if (inst.spawnedIds[slot] != 0u) continue;
                        if (inst.respawnAtTick[slot] != 0u && inst.respawnAtTick[slot] > tick) continue;
                        uint32_t eid = nextMobEntityId++;
                        Mob m;
                        m.entityId = eid;
                        m.zoneId = def.zoneId;
                        m.position[0] = m.spawnPosition[0] = def.position[0];
                        m.position[1] = m.spawnPosition[1] = def.position[1];
                        m.position[2] = m.spawnPosition[2] = def.position[2];
                        m.spawnerInstanceIndex = static_cast<int32_t>(si);
                        m.spawnerSlot = static_cast<int32_t>(slot);
                        mobs.push_back(m);
                        engine::network::ReplicationEntityState st{};
                        st.entityId = eid;
                        st.archetypeId = def.archetypeId;
                        st.position[0] = def.position[0];
                        st.position[1] = def.position[1];
                        st.position[2] = def.position[2];
                        entityStates[eid] = st;
                        entityCombat[eid] = EntityCombat{};
                        inst.spawnedIds[slot] = eid;
                    }
                } else {
                    for (size_t slot = 0; slot < inst.spawnedIds.size(); ++slot) {
                        uint32_t eid = inst.spawnedIds[slot];
                        if (eid == 0u) continue;
                        inst.spawnedIds[slot] = 0u;
                        inst.respawnAtTick[slot] = tick;
                        threatTable.Clear(eid);
                        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [eid](const Mob& m) { return m.entityId == eid; }), mobs.end());
                        entityStates.erase(eid);
                        entityCombat.erase(eid);
                        std::vector<uint8_t> despawnPkt;
                        engine::network::SerializeDespawn(static_cast<uint64_t>(eid), despawnPkt);
                        for (size_t i = 0; i < clients.size(); ++i)
                            engine::network::UdpSendTo(fd, despawnPkt.data(), despawnPkt.size(), &clients[i].address);
                    }
                }
            }
            for (size_t ei = 0; ei < eventInstances.size(); ++ei) {
                EventInstanceState& inst = eventInstances[ei];
                const engine::event::EventDef& def = eventDefs[ei];
                bool zoneHasPlayers = false;
                for (size_t c = 0; c < clients.size(); ++c)
                    if (clientZoneIds[c] == def.zoneId) { zoneHasPlayers = true; break; }
                if (!zoneHasPlayers) continue;
                auto sendEventStateToZone = [&](engine::network::EventStateEnum st, uint32_t phaseIdx, uint32_t phaseCnt, const char* text) {
                    std::vector<uint8_t> pkt;
                    engine::network::SerializeEventState(def.id, st, phaseIdx, phaseCnt, text, pkt);
                    for (size_t c = 0; c < clients.size(); ++c)
                        if (clientZoneIds[c] == def.zoneId)
                            engine::network::UdpSendTo(fd, pkt.data(), pkt.size(), &clients[c].address);
                };
                auto spawnPhaseWave = [&](const engine::event::EventPhaseDef& phase) {
                    for (const auto& sp : phase.spawns) {
                        for (uint32_t n = 0; n < sp.count; ++n) {
                            uint32_t eid = nextMobEntityId++;
                            Mob m;
                            m.entityId = eid;
                            m.zoneId = def.zoneId;
                            m.position[0] = m.spawnPosition[0] = sp.position[0];
                            m.position[1] = m.spawnPosition[1] = sp.position[1];
                            m.position[2] = m.spawnPosition[2] = sp.position[2];
                            m.spawnerInstanceIndex = -1;
                            m.spawnerSlot = -1;
                            mobs.push_back(m);
                            engine::network::ReplicationEntityState st{};
                            st.entityId = eid;
                            st.archetypeId = sp.archetypeId;
                            st.position[0] = sp.position[0];
                            st.position[1] = sp.position[1];
                            st.position[2] = sp.position[2];
                            entityStates[eid] = st;
                            entityCombat[eid] = EntityCombat{};
                        }
                    }
                };
                if (inst.state == 0u) {
                    if (tick < inst.cooldownEndTick) continue;
                    bool trigger = false;
                    if (def.triggerType == engine::event::EventTriggerType::Time) {
                        uint32_t intervalTicks = static_cast<uint32_t>(def.triggerIntervalSec * 20.0f);
                        if (intervalTicks == 0u) intervalTicks = 1u;
                        if (tick >= inst.lastTriggerTick + intervalTicks) trigger = true;
                    } else {
                        if (std::rand() / static_cast<double>(RAND_MAX) < static_cast<double>(def.triggerChancePerTick)) trigger = true;
                    }
                    if (!trigger) continue;
                    inst.state = 1u;
                    inst.currentPhase = 0u;
                    inst.participants.clear();
                    for (size_t c = 0; c < clients.size(); ++c)
                        if (clientZoneIds[c] == def.zoneId) inst.participants.push_back(clients[c].clientId);
                    inst.lastTriggerTick = tick;
                    uint32_t totalPhases = static_cast<uint32_t>(def.phases.size());
                    if (totalPhases > 0u) {
                        const auto& phase = def.phases[0];
                        inst.phaseEndTick = tick + static_cast<uint32_t>(phase.durationSec * 20.0f);
                        spawnPhaseWave(phase);
                        sendEventStateToZone(engine::network::EventStateEnum::Active, 0u, totalPhases, phase.label.c_str());
                    } else {
                        inst.phaseEndTick = tick;
                    }
                } else {
                    if (tick < inst.phaseEndTick) continue;
                    uint32_t totalPhases = static_cast<uint32_t>(def.phases.size());
                    inst.currentPhase++;
                    if (inst.currentPhase >= totalPhases) {
                        for (uint32_t cid : inst.participants) {
                            for (const auto& r : def.rewards)
                                playerInventory[cid][r.itemId] += r.count;
                            std::vector<engine::network::InventoryDeltaEntry> delta;
                            for (const auto& r : def.rewards)
                                delta.push_back({r.itemId, r.count});
                            if (!delta.empty()) {
                                std::vector<uint8_t> deltaPkt;
                                engine::network::SerializeInventoryDelta(delta.data(), delta.size(), deltaPkt);
                                for (size_t c = 0; c < clients.size(); ++c)
                                    if (clients[c].clientId == cid) {
                                        engine::network::UdpSendTo(fd, deltaPkt.data(), deltaPkt.size(), &clients[c].address);
                                        break;
                                    }
                            }
                        }
                        inst.state = 0u;
                        inst.cooldownEndTick = tick + static_cast<uint32_t>(def.cooldownSec * 20.0f);
                        inst.participants.clear();
                        sendEventStateToZone(engine::network::EventStateEnum::Completed, 0u, 0u, "Complete");
                    } else {
                        const auto& phase = def.phases[inst.currentPhase];
                        inst.phaseEndTick = tick + static_cast<uint32_t>(phase.durationSec * 20.0f);
                        spawnPhaseWave(phase);
                        sendEventStateToZone(engine::network::EventStateEnum::Active, inst.currentPhase, totalPhases, phase.label.c_str());
                    }
                }
            }
            auto fireQuestEvent = [&](uint32_t cid, engine::quest::QuestStepType stepType, const std::string& targetStr, uint32_t count) {
                const engine::network::PeerAddress* addr = nullptr;
                for (size_t i = 0; i < clients.size(); ++i) if (clients[i].clientId == cid) { addr = &clients[i].address; break; }
                if (!addr) return;
                auto pit = playerQuestProgress.find(cid);
                if (pit == playerQuestProgress.end()) return;
                std::vector<std::tuple<uint32_t, uint32_t, uint32_t, bool>> toSend;
                for (auto it = pit->second.begin(); it != pit->second.end(); ) {
                    auto qit = questDefsById.find(it->first);
                    if (qit == questDefsById.end() || it->second.stepIndex >= qit->second.steps.size()) { ++it; continue; }
                    const auto& step = qit->second.steps[it->second.stepIndex];
                    if (step.type != stepType || step.target != targetStr) { ++it; continue; }
                    it->second.counter += count;
                    while (it->second.stepIndex < qit->second.steps.size() && it->second.counter >= qit->second.steps[it->second.stepIndex].count) {
                        it->second.counter -= qit->second.steps[it->second.stepIndex].count;
                        it->second.stepIndex++;
                    }
                    bool completed = (it->second.stepIndex >= qit->second.steps.size());
                    toSend.push_back({it->first, it->second.stepIndex, it->second.counter, completed});
                    if (completed) { playerCompletedQuests[cid].insert(it->first); it = pit->second.erase(it); } else ++it;
                }
                for (const auto& t : toSend) {
                    std::vector<uint8_t> d;
                    engine::network::SerializeQuestDelta(std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t), d);
                    engine::network::UdpSendTo(fd, d.data(), d.size(), addr);
                }
            };
            while (engine::network::UdpRecvFrom(fd, buf, kRecvBuf, &from) > 0) {
                if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::Connect)) {
                    uint64_t reqCharacterId = 0u;
                    if (kRecvBuf >= 9)
                        std::memcpy(&reqCharacterId, buf + 1, 8);
                    engine::network::ServerClient c;
                    c.address = from;
                    c.clientId = nextId++;
                    engine::persistence::CharacterState charState{};
                    if (reqCharacterId == 0u) {
                        charState.zoneId = 0;
                        charState.posX = 0.f;
                        charState.posY = 0.f;
                        charState.posZ = 0.f;
                        charState.hp = kDefaultMaxHp;
                        charState.maxHp = kDefaultMaxHp;
                        c.characterId = engine::persistence::InsertCharacter(charState);
                        if (c.characterId <= 0) continue;
                    } else {
                        if (!engine::persistence::LoadCharacter(static_cast<int64_t>(reqCharacterId), charState))
                            continue;
                        c.characterId = static_cast<int64_t>(reqCharacterId);
                    }
                    clients.push_back(c);
                    clientPositions.push_back({charState.posX, charState.posY, charState.posZ});
                    clientZoneIds.push_back(charState.zoneId);
                    clientInterestSets.push_back(engine::world::ClientInterestSet{});
                    engine::network::ReplicationEntityState st{};
                    st.entityId = c.clientId;
                    st.archetypeId = 0;
                    st.position[0] = charState.posX;
                    st.position[1] = charState.posY;
                    st.position[2] = charState.posZ;
                    entityStates[c.clientId] = st;
                    EntityCombat ec{};
                    ec.hp = charState.hp;
                    ec.maxHp = charState.maxHp;
                    entityCombat[c.clientId] = ec;
                    engine::persistence::LoadInventory(c.characterId, playerInventory[c.clientId]);

                    uint8_t ack[29];
                    ack[0] = static_cast<uint8_t>(engine::network::MsgType::ConnectAck);
                    std::memcpy(ack + 1, &c.clientId, 4);
                    std::memcpy(ack + 5, &c.characterId, 8);
                    std::memcpy(ack + 13, &charState.zoneId, 4);
                    std::memcpy(ack + 17, &charState.posX, 4);
                    std::memcpy(ack + 21, &charState.posY, 4);
                    std::memcpy(ack + 25, &charState.posZ, 4);
                    engine::network::UdpSendTo(fd, ack, sizeof(ack), &from);
                    std::vector<engine::network::InventoryDeltaEntry> initialInv;
                    for (const auto& p : playerInventory[c.clientId])
                        initialInv.push_back({p.first, p.second});
                    if (!initialInv.empty()) {
                        std::vector<uint8_t> invPkt;
                        engine::network::SerializeInventoryDelta(initialInv.data(), initialInv.size(), invPkt);
                        engine::network::UdpSendTo(fd, invPkt.data(), invPkt.size(), &from);
                    }
                } else if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::Logout)) {
                    size_t idx = clients.size();
                    for (size_t i = 0; i < clients.size(); ++i) {
                        if (PeerAddressEqual(clients[i].address, from)) { idx = i; break; }
                    }
                    if (idx < clients.size()) {
                        uint32_t clientId = clients[idx].clientId;
                        int64_t characterId = clients[idx].characterId;
                        engine::persistence::CharacterState cs{};
                        cs.zoneId = clientZoneIds[idx];
                        cs.posX = clientPositions[idx][0];
                        cs.posY = clientPositions[idx][1];
                        cs.posZ = clientPositions[idx][2];
                        cs.hp = entityCombat[clientId].hp;
                        cs.maxHp = entityCombat[clientId].maxHp;
                        engine::persistence::SaveCharacter(characterId, cs);
                        engine::persistence::SaveInventory(characterId, playerInventory[clientId]);
                        clients.erase(clients.begin() + idx);
                        clientPositions.erase(clientPositions.begin() + idx);
                        clientZoneIds.erase(clientZoneIds.begin() + idx);
                        clientInterestSets.erase(clientInterestSets.begin() + idx);
                        entityStates.erase(clientId);
                        entityCombat.erase(clientId);
                        playerInventory.erase(clientId);
                        playerQuestProgress.erase(clientId);
                        playerCompletedQuests.erase(clientId);
                        clientTriggersInsidePrev.erase(clientId);
                    }
                } else if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::AcceptQuest) && kRecvBuf >= 5) {
                    uint32_t questId = 0;
                    if (engine::network::ParseAcceptQuest(buf + 1, 4u, questId)) {
                        size_t ci = clients.size();
                        for (size_t i = 0; i < clients.size(); ++i) {
                            if (PeerAddressEqual(clients[i].address, from)) { ci = i; break; }
                        }
                        if (ci < clients.size()) {
                            uint32_t cid = clients[ci].clientId;
                            auto qit = questDefsById.find(questId);
                            if (qit != questDefsById.end() && playerCompletedQuests[cid].count(questId) == 0u
                                && playerQuestProgress[cid].count(questId) == 0u) {
                                bool prereqsOk = true;
                                for (uint32_t pr : qit->second.prereqs)
                                    if (playerCompletedQuests[cid].count(pr) == 0u) { prereqsOk = false; break; }
                                if (prereqsOk) {
                                    playerQuestProgress[cid][questId] = QuestProgress{0, 0};
                                    std::vector<uint8_t> d;
                                    engine::network::SerializeQuestDelta(questId, 0, 0, false, d);
                                    engine::network::UdpSendTo(fd, d.data(), d.size(), &clients[ci].address);
                                }
                            }
                        }
                    }
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
                                    if (tc.isDead && tId >= kFirstMobEntityId && tId < kFirstLootBagEntityId) {
                                        LootBag bag;
                                        bag.entityId = nextLootBagId++;
                                        bag.zoneId = targetZone;
                                        bag.position[0] = entityStates[tId].position[0];
                                        bag.position[1] = entityStates[tId].position[1];
                                        bag.position[2] = entityStates[tId].position[2];
                                        bag.ownerId = 0;
                                        engine::loot::GenerateLoot(defaultLootTable, bag.items);
                                        engine::network::ReplicationEntityState st{};
                                        st.entityId = bag.entityId;
                                        st.archetypeId = kLootBagArchetypeId;
                                        st.position[0] = bag.position[0];
                                        st.position[1] = bag.position[1];
                                        st.position[2] = bag.position[2];
                                        entityStates[bag.entityId] = st;
                                        lootBags.push_back(bag);
                                    }
                                    std::string killTarget = "archetype:" + std::to_string(static_cast<unsigned>(entityStates[tId].archetypeId));
                                    fireQuestEvent(aId, engine::quest::QuestStepType::Kill, killTarget, 1);
                                    auto mobIt = std::find_if(mobs.begin(), mobs.end(), [tId](const Mob& m) { return m.entityId == tId; });
                                    if (mobIt != mobs.end() && mobIt->spawnerInstanceIndex >= 0) {
                                        SpawnerInstance& si = spawnerInstances[static_cast<size_t>(mobIt->spawnerInstanceIndex)];
                                        int32_t slot = mobIt->spawnerSlot;
                                        if (slot >= 0 && slot < static_cast<int32_t>(si.spawnedIds.size())) {
                                            uint32_t respawnTicks = static_cast<uint32_t>(si.def.respawnSec * 20.0f);
                                            si.respawnAtTick[static_cast<size_t>(slot)] = tick + respawnTicks;
                                            si.spawnedIds[static_cast<size_t>(slot)] = 0u;
                                        }
                                        threatTable.Clear(tId);
                                        mobs.erase(mobIt);
                                        entityStates.erase(tId);
                                        entityCombat.erase(tId);
                                        std::vector<uint8_t> despawnPkt;
                                        engine::network::SerializeDespawn(static_cast<uint64_t>(tId), despawnPkt);
                                        for (size_t i = 0; i < clients.size(); ++i)
                                            engine::network::UdpSendTo(fd, despawnPkt.data(), despawnPkt.size(), &clients[i].address);
                                    }
                                }
                            }
                        }
                    }
                } else if (buf[0] == static_cast<uint8_t>(engine::network::MsgType::PickupRequest) && kRecvBuf >= 9) {
                    uint64_t bagId64 = 0;
                    if (engine::network::ParsePickupRequest(buf + 1, 8u, bagId64)) {
                        uint32_t bagId = static_cast<uint32_t>(bagId64);
                        size_t clientIdx = clients.size();
                        for (size_t i = 0; i < clients.size(); ++i) {
                            if (PeerAddressEqual(clients[i].address, from)) { clientIdx = i; break; }
                        }
                        if (clientIdx < clients.size()) {
                            uint32_t clientId = clients[clientIdx].clientId;
                            LootBag* bagPtr = nullptr;
                            size_t bagIdx = 0;
                            for (size_t i = 0; i < lootBags.size(); ++i) {
                                if (lootBags[i].entityId == bagId) { bagPtr = &lootBags[i]; bagIdx = i; break; }
                            }
                            if (bagPtr && (bagPtr->ownerId == 0u || bagPtr->ownerId == clientId)) {
                                float px = clientPositions[clientIdx][0], pz = clientPositions[clientIdx][2];
                                float bx = bagPtr->position[0], bz = bagPtr->position[2];
                                float dx = px - bx, dz = pz - bz;
                                if (std::sqrt(dx * dx + dz * dz) <= kPickupRange) {
                                    for (const auto& e : bagPtr->items)
                                        playerInventory[clientId][e.itemId] += e.count;
                                    std::vector<engine::network::InventoryDeltaEntry> delta;
                                    for (const auto& e : bagPtr->items)
                                        delta.push_back({e.itemId, e.count});
                                    std::vector<uint8_t> deltaPkt;
                                    engine::network::SerializeInventoryDelta(delta.data(), delta.size(), deltaPkt);
                                    engine::network::UdpSendTo(fd, deltaPkt.data(), deltaPkt.size(), &clients[clientIdx].address);
                                    entityStates.erase(bagId);
                                    lootBags.erase(lootBags.begin() + bagIdx);
                                    std::vector<uint8_t> despawnPkt;
                                    engine::network::SerializeDespawn(bagId64, despawnPkt);
                                    for (size_t i = 0; i < clients.size(); ++i)
                                        engine::network::UdpSendTo(fd, despawnPkt.data(), despawnPkt.size(), &clients[i].address);
                                    for (const auto& e : bagPtr->items)
                                        fireQuestEvent(clientId, engine::quest::QuestStepType::Collect, "item:" + std::to_string(e.itemId), e.count);
                                }
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

            for (size_t i = 0; i < clients.size(); ++i) {
                std::set<std::string> nowInside;
                auto vit = volumesPerZone.find(clientZoneIds[i]);
                if (vit != volumesPerZone.end()) {
                    for (const auto& vol : vit->second) {
                        if (vol.type != engine::world::VolumeType::Trigger) continue;
                        if (!engine::world::PointInVolume(clientPositions[i][0], clientPositions[i][1], clientPositions[i][2], vol)) continue;
                        if (!vol.actionId.empty()) nowInside.insert(vol.actionId);
                    }
                }
                uint32_t cid = clients[i].clientId;
                for (const auto& actionId : nowInside) {
                    if (clientTriggersInsidePrev[cid].count(actionId) == 0u) {
                        fireQuestEvent(cid, engine::quest::QuestStepType::Talk, actionId, 1);
                        fireQuestEvent(cid, engine::quest::QuestStepType::Enter, actionId, 1);
                    }
                }
                clientTriggersInsidePrev[cid] = std::move(nowInside);
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
            for (const auto& b : lootBags) {
                if (zoneGrids.find(b.zoneId) == zoneGrids.end())
                    zoneGrids[b.zoneId] = engine::world::CellGrid(0, 0);
                zoneGrids[b.zoneId].Insert(b.entityId, b.position[0], b.position[2]);
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
