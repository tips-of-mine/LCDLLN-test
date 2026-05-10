// Wave 5 Persistence (Phase 3.17b) - Implementation MysqlLootStore.

#include "src/masterd/loot/MysqlLootStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

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
		if (!mysql) return out;

		const char* sql =
			"SELECT table_id, name, description FROM loot_tables "
			"ORDER BY table_id ASC";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlLootStore] LoadAllTables query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			LootTableRow r;
			if (row[0]) r.tableId     = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.name        = row[1];
			if (row[2]) r.description = row[2];
			out.push_back(std::move(r));
		}
		engine::server::db::DbFreeResult(res);
		LOG_INFO(Net, "[MysqlLootStore] LoadAllTables loaded {} tables", out.size());
		return out;
	}

	std::vector<LootEntryRow> MysqlLootStore::LoadEntriesForTable(uint32_t tableId) const
	{
		std::vector<LootEntryRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"SELECT entry_id, table_id, item_template_id, item_name, "
			"drop_chance_pct, min_count, max_count "
			"FROM loot_table_entries WHERE table_id = %u ORDER BY entry_id ASC",
			tableId);
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlLootStore] LoadEntriesForTable query failed tableId={}", tableId);
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			LootEntryRow r;
			if (row[0]) r.entryId         = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.tableId          = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) r.itemTemplateId   = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
			if (row[3]) r.itemName         = row[3];
			if (row[4]) r.dropChancePct    = static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10));
			if (row[5]) r.minCount          = static_cast<uint32_t>(std::strtoul(row[5], nullptr, 10));
			if (row[6]) r.maxCount          = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
			out.push_back(std::move(r));
		}
		engine::server::db::DbFreeResult(res);
		LOG_INFO(Net, "[MysqlLootStore] LoadEntriesForTable tableId={} loaded {} entries", tableId, out.size());
		return out;
	}
}
