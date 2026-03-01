/**
 * @file Replication.cpp
 * @brief Spawn/Despawn/Snapshot serialization and build-by-interest (M13.3).
 */

#include "engine/network/Replication.h"

#include <cstring>
#include <unordered_set>

namespace engine::network {

namespace {

constexpr size_t kSpawnHeader = 1 + 8 + 4 + 12 + 4 + 12;  // type + entityId + archetypeId + pos + rotY + vel
constexpr size_t kDespawnHeader = 1 + 8;
constexpr size_t kEntityStateSize = 8 + 4 + 12 + 4 + 12;   // entityId + archetypeId + pos + rotY + vel

} // namespace

size_t SerializeSpawn(uint64_t entityId, uint32_t archetypeId,
                      const float position[3], float rotationY, const float velocity[3],
                      std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + kSpawnHeader);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::Spawn);
    std::memcpy(p + 1, &entityId, 8);
    std::memcpy(p + 9, &archetypeId, 4);
    std::memcpy(p + 13, position, 12);
    std::memcpy(p + 25, &rotationY, 4);
    std::memcpy(p + 29, velocity, 12);
    return kSpawnHeader;
}

size_t SerializeDespawn(uint64_t entityId, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + kDespawnHeader);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::Despawn);
    std::memcpy(p + 1, &entityId, 8);
    return kDespawnHeader;
}

size_t SerializeSnapshotWithStates(uint32_t tick, const std::vector<ReplicationEntityState>& states,
                                   std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    size_t payload = 1 + 4 + 4 + states.size() * kEntityStateSize;
    outBuffer.resize(off + payload);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::Snapshot);
    std::memcpy(p + 1, &tick, 4);
    uint32_t n = static_cast<uint32_t>(states.size());
    std::memcpy(p + 5, &n, 4);
    for (size_t i = 0; i < states.size(); ++i) {
        uint8_t* q = p + 9 + i * kEntityStateSize;
        std::memcpy(q, &states[i].entityId, 8);
        std::memcpy(q + 8, &states[i].archetypeId, 4);
        std::memcpy(q + 12, states[i].position, 12);
        std::memcpy(q + 24, &states[i].rotationY, 4);
        std::memcpy(q + 28, states[i].velocity, 12);
    }
    return payload;
}

void BuildReplicationForClient(
    uint32_t snapshotTick,
    const std::vector<std::array<int32_t, 4>>& cellsEntered,
    const std::vector<std::array<int32_t, 4>>& cellsLeft,
    const std::vector<std::array<int32_t, 4>>& currentCells,
    GetEntityIdsInCellFn getEntityIdsInCell,
    GetEntityStateFn getEntityState,
    std::vector<std::vector<uint8_t>>& outSpawns,
    std::vector<std::vector<uint8_t>>& outDespawns,
    std::vector<uint8_t>& outSnapshot) {
    outSpawns.clear();
    outDespawns.clear();
    outSnapshot.clear();

    for (const auto& c : cellsEntered) {
        std::vector<uint64_t> ids = getEntityIdsInCell(c[0], c[1], c[2], c[3]);
        for (uint64_t eid : ids) {
            ReplicationEntityState st;
            if (!getEntityState(eid, st)) continue;
            std::vector<uint8_t> buf;
            SerializeSpawn(st.entityId, st.archetypeId, st.position, st.rotationY, st.velocity, buf);
            outSpawns.push_back(std::move(buf));
        }
    }

    for (const auto& c : cellsLeft) {
        std::vector<uint64_t> ids = getEntityIdsInCell(c[0], c[1], c[2], c[3]);
        for (uint64_t eid : ids) {
            std::vector<uint8_t> buf;
            SerializeDespawn(eid, buf);
            outDespawns.push_back(std::move(buf));
        }
    }

    std::unordered_set<uint64_t> seen;
    std::vector<ReplicationEntityState> states;
    for (const auto& c : currentCells) {
        std::vector<uint64_t> ids = getEntityIdsInCell(c[0], c[1], c[2], c[3]);
        for (uint64_t eid : ids) {
            if (seen.count(eid)) continue;
            seen.insert(eid);
            ReplicationEntityState st;
            if (!getEntityState(eid, st)) continue;
            states.push_back(st);
        }
    }
    if (!states.empty())
        SerializeSnapshotWithStates(snapshotTick, states, outSnapshot);
}

bool ParseSpawn(const uint8_t* data, size_t size, ReplicationEntityState& out) {
    if (size < kSpawnHeader - 1) return false;
    std::memcpy(&out.entityId, data, 8);
    std::memcpy(&out.archetypeId, data + 8, 4);
    std::memcpy(out.position, data + 12, 12);
    std::memcpy(&out.rotationY, data + 24, 4);
    std::memcpy(out.velocity, data + 28, 12);
    return true;
}

bool ParseDespawn(const uint8_t* data, size_t size, uint64_t& outEntityId) {
    if (size < 8) return false;
    std::memcpy(&outEntityId, data, 8);
    return true;
}

bool ParseSnapshotWithStates(const uint8_t* data, size_t size, uint32_t& outTick, std::vector<ReplicationEntityState>& outStates) {
    if (size < 8) return false;
    std::memcpy(&outTick, data, 4);
    uint32_t n = 0;
    std::memcpy(&n, data + 4, 4);
    outStates.clear();
    size_t need = 8 + n * kEntityStateSize;
    if (size < need) return false;
    outStates.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* q = data + 8 + i * kEntityStateSize;
        std::memcpy(&outStates[i].entityId, q, 8);
        std::memcpy(&outStates[i].archetypeId, q + 8, 4);
        std::memcpy(outStates[i].position, q + 12, 12);
        std::memcpy(&outStates[i].rotationY, q + 24, 4);
        std::memcpy(outStates[i].velocity, q + 28, 12);
    }
    return true;
}

} // namespace engine::network
