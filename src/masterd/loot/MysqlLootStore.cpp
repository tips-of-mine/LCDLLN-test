// Wave 5 Persistence (Phase 3.17b) - Implementation MysqlLootStore.
// N1-E : converti en prepared statements (2 SELECTs).

#include "src/masterd/loot/MysqlLootStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::loot_db
{
	bool MysqlLootStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<LootTableRow> MysqlLootStore::LoadAllTables() const
	{
		std::vector<LootTableRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT table_id, name, description FROM loot_tables ORDER BY table_id ASC");
		if (!stmt || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlLootStore] LoadAllTables query failed");
			return out;
		}
		while (stmt->FetchRow())
		{
			LootTableRow r;
			r.tableId     = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.name        = stmt->GetString(1);
			r.description = stmt->GetString(2);
			out.push_back(std::move(r));
		}
		LOG_INFO(Net, "[MysqlLootStore] LoadAllTables loaded {} tables", out.size());
		return out;
	}

	std::vector<LootEntryRow> MysqlLootStore::LoadEntriesForTable(uint32_t tableId) const
	{
		std::vector<LootEntryRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT entry_id, table_id, item_template_id, item_name, "
			"drop_chance_pct, min_count, max_count "
			"FROM loot_table_entries WHERE table_id = ? ORDER BY entry_id ASC");
		if (!stmt || !stmt->Bind(0, tableId) || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlLootStore] LoadEntriesForTable query failed tableId={}", tableId);
			return out;
		}
		while (stmt->FetchRow())
		{
			LootEntryRow r;
			r.entryId         = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.tableId         = static_cast<uint32_t>(stmt->GetUInt64(1));
			r.itemTemplateId  = static_cast<uint32_t>(stmt->GetUInt64(2));
			r.itemName        = stmt->GetString(3);
			r.dropChancePct   = static_cast<uint32_t>(stmt->GetUInt64(4));
			r.minCount        = static_cast<uint32_t>(stmt->GetUInt64(5));
			r.maxCount        = static_cast<uint32_t>(stmt->GetUInt64(6));
			out.push_back(std::move(r));
		}
		LOG_INFO(Net, "[MysqlLootStore] LoadEntriesForTable tableId={} loaded {} entries", tableId, out.size());
		return out;
	}
}
