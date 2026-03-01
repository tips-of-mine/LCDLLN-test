/**
 * @file LootTable.cpp
 * @brief Loot generation implementation (M14.3).
 */

#include "engine/loot/LootTable.h"

namespace engine::loot {

void GenerateLoot(const LootTableData& table, std::vector<LootEntry>& out) {
    out.clear();
    for (const auto& e : table.entries)
        if (e.itemId != 0u && e.count != 0u)
            out.push_back(e);
}

} // namespace engine::loot
