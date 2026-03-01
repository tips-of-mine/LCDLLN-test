#pragma once

/**
 * @file LootProtocol.h
 * @brief PickupRequest (client->server) and InventoryDelta (server->client) serialization (M14.3).
 */

#include "engine/network/Protocol.h"
#include <cstdint>
#include <vector>

namespace engine::network {

/** @brief Serializes PickupRequest (type + lootBagEntityId 8 bytes). Returns bytes written. */
size_t SerializePickupRequest(uint64_t lootBagEntityId, std::vector<uint8_t>& outBuffer);

/** @brief Parses PickupRequest payload (type already consumed). Returns true if size >= 8. */
bool ParsePickupRequest(const uint8_t* data, size_t size, uint64_t& outLootBagEntityId);

/** @brief One inventory delta entry: item added (itemId + count). */
struct InventoryDeltaEntry {
    uint32_t itemId = 0;
    uint32_t count = 0;
};

/** @brief Serializes InventoryDelta (type + 4 bytes numEntries + 8 bytes per entry). Returns bytes written. */
size_t SerializeInventoryDelta(const InventoryDeltaEntry* entries, size_t numEntries, std::vector<uint8_t>& outBuffer);

/** @brief Parses InventoryDelta payload (type already consumed). Returns true if size valid. */
bool ParseInventoryDelta(const uint8_t* data, size_t size, std::vector<InventoryDeltaEntry>& outEntries);

} // namespace engine::network
