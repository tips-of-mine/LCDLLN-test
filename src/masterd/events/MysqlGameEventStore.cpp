// Wave 5 Persistence (Phase 5.31b) - Implementation MysqlGameEventStore.

#include "src/masterd/events/MysqlGameEventStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdlib>

namespace engine::server::events
{
	bool MysqlGameEventStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<GameEventRow> MysqlGameEventStore::LoadAll() const
	{
		std::vector<GameEventRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		const char* sql =
			"SELECT event_id, name, start_ts_ms, duration_ms, recur_ms, "
			"requires_lunar_phase_mask FROM game_events ORDER BY event_id ASC";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlGameEventStore] LoadAll query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			GameEventRow r;
			if (row[0]) r.eventId    = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.name       = row[1];
			if (row[2]) r.startTsMs  = std::strtoull(row[2], nullptr, 10);
			if (row[3]) r.durationMs = std::strtoull(row[3], nullptr, 10);
			if (row[4]) r.recurMs    = std::strtoull(row[4], nullptr, 10);
			if (row[5]) r.requiresLunarPhaseMask =
				static_cast<uint16_t>(std::strtoul(row[5], nullptr, 10));
			out.push_back(std::move(r));
		}
		engine::server::db::DbFreeResult(res);
		LOG_INFO(Net, "[MysqlGameEventStore] LoadAll loaded {} events", out.size());
		return out;
	}
}
