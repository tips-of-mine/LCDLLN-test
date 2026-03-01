#pragma once

/**
 * @file LootTable.h
 * @brief Loot table data and loot generation (M14.3).
 */

#include <cstdint>
#include <vector>

namespace engine::loot {

/** @brief Single loot entry: item id and count. */
struct LootEntry {
    uint32_t itemId = 0;
    uint32_t count = 0;
};

/** @brief Loot table: fixed list of entries. GenerateLoot returns a copy of entries (MVP: no chance roll). */
struct LootTableData {
    std::vector<LootEntry> entries;
};

/**
 * @brief Generates loot from a table. Returns one item per entry (MVP: deterministic).
 * @param table  Loot table.
 * @param out    Output list of (itemId, count).
 */
void GenerateLoot(const LootTableData& table, std::vector<LootEntry>& out);

} // namespace engine::loot
