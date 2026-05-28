// Wave 5 Persistence (Phase 5.31b) - Implementation MysqlGameEventStore.
// N1-E : converti en prepared statements (1 SELECT no-param).

#include "src/masterd/events/MysqlGameEventStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

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
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT event_id, name, start_ts_ms, duration_ms, recur_ms, "
			"requires_lunar_phase_mask FROM game_events ORDER BY event_id ASC");
		if (!stmt || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlGameEventStore] LoadAll query failed");
			return out;
		}
		while (stmt->FetchRow())
		{
			GameEventRow r;
			r.eventId    = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.name       = stmt->GetString(1);
			r.startTsMs  = stmt->GetUInt64(2);
			r.durationMs = stmt->GetUInt64(3);
			r.recurMs    = stmt->GetUInt64(4);
			r.requiresLunarPhaseMask = static_cast<uint16_t>(stmt->GetUInt64(5));
			out.push_back(std::move(r));
		}
		LOG_INFO(Net, "[MysqlGameEventStore] LoadAll loaded {} events", out.size());
		return out;
	}
}
