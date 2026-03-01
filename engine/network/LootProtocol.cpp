/**
 * @file LootProtocol.cpp
 * @brief PickupRequest / InventoryDelta serialization (M14.3).
 */

#include "engine/network/LootProtocol.h"
#include <cstring>

namespace engine::network {

size_t SerializePickupRequest(uint64_t lootBagEntityId, std::vector<uint8_t>& outBuffer) {
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1u + 8u);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::PickupRequest);
    std::memcpy(p + 1, &lootBagEntityId, 8);
    return 9u;
}

bool ParsePickupRequest(const uint8_t* data, size_t size, uint64_t& outLootBagEntityId) {
    if (size < 8u) return false;
    std::memcpy(&outLootBagEntityId, data, 8);
    return true;
}

size_t SerializeInventoryDelta(const InventoryDeltaEntry* entries, size_t numEntries, std::vector<uint8_t>& outBuffer) {
    size_t payload = 4u + numEntries * 8u;
    size_t off = outBuffer.size();
    outBuffer.resize(off + 1u + payload);
    uint8_t* p = outBuffer.data() + off;
    p[0] = static_cast<uint8_t>(MsgType::InventoryDelta);
    uint32_t n = static_cast<uint32_t>(numEntries);
    std::memcpy(p + 1, &n, 4);
    for (size_t i = 0; i < numEntries; ++i) {
        std::memcpy(p + 5 + i * 8, &entries[i].itemId, 4);
        std::memcpy(p + 5 + i * 8 + 4, &entries[i].count, 4);
    }
    return 1u + payload;
}

bool ParseInventoryDelta(const uint8_t* data, size_t size, std::vector<InventoryDeltaEntry>& outEntries) {
    if (size < 4u) return false;
    uint32_t n = 0;
    std::memcpy(&n, data, 4);
    if (size < 4u + n * 8u) return false;
    outEntries.clear();
    outEntries.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        InventoryDeltaEntry e;
        std::memcpy(&e.itemId, data + 4 + i * 8, 4);
        std::memcpy(&e.count, data + 4 + i * 8 + 4, 4);
        outEntries.push_back(e);
    }
    return true;
}

} // namespace engine::network
