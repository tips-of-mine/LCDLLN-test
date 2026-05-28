// Wave 5 Persistence (Phase 5.36b) - Implementation MysqlOutdoorPvpStore.
// N1-F : converti en prepared statements (2 SELECTs no-param + 2 INSERTs upsert).

#include "src/masterd/outdoorpvp/MysqlOutdoorPvpStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::outdoorpvp_db
{
	bool MysqlOutdoorPvpStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<ObjectiveRow> MysqlOutdoorPvpStore::LoadStates() const
	{
		std::vector<ObjectiveRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT zone_id, objective_id, owner, capture_pct, capturing_by "
			"FROM outdoor_pvp_state");
		if (!stmt || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] LoadStates query failed");
			return out;
		}
		while (stmt->FetchRow())
		{
			ObjectiveRow r;
			r.zoneId      = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.objectiveId = static_cast<uint32_t>(stmt->GetUInt64(1));
			r.owner       = static_cast<uint8_t>(stmt->GetUInt64(2));
			r.capturePct  = static_cast<uint32_t>(stmt->GetUInt64(3));
			r.capturingBy = static_cast<uint8_t>(stmt->GetUInt64(4));
			out.push_back(r);
		}
		return out;
	}

	std::vector<ScoreRow> MysqlOutdoorPvpStore::LoadScores() const
	{
		std::vector<ScoreRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT zone_id, faction, score FROM outdoor_pvp_scores");
		if (!stmt || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] LoadScores query failed");
			return out;
		}
		while (stmt->FetchRow())
		{
			ScoreRow r;
			r.zoneId  = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.faction = static_cast<uint8_t>(stmt->GetUInt64(1));
			r.score   = static_cast<uint32_t>(stmt->GetUInt64(2));
			out.push_back(r);
		}
		return out;
	}

	bool MysqlOutdoorPvpStore::UpsertObjective(const ObjectiveRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO outdoor_pvp_state (zone_id, objective_id, owner, capture_pct, capturing_by) "
			"VALUES (?, ?, ?, ?, ?) "
			"ON DUPLICATE KEY UPDATE "
			"owner = VALUES(owner), capture_pct = VALUES(capture_pct), "
			"capturing_by = VALUES(capturing_by)");
		const bool ok = stmt
			&& stmt->Bind(0, row.zoneId)
			&& stmt->Bind(1, row.objectiveId)
			&& stmt->Bind(2, static_cast<uint32_t>(row.owner))
			&& stmt->Bind(3, row.capturePct)
			&& stmt->Bind(4, static_cast<uint32_t>(row.capturingBy))
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] UpsertObjective failed zid={} oid={}",
				row.zoneId, row.objectiveId);
		return ok;
	}

	bool MysqlOutdoorPvpStore::UpsertScore(const ScoreRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO outdoor_pvp_scores (zone_id, faction, score) "
			"VALUES (?, ?, ?) "
			"ON DUPLICATE KEY UPDATE score = VALUES(score)");
		const bool ok = stmt
			&& stmt->Bind(0, row.zoneId)
			&& stmt->Bind(1, static_cast<uint32_t>(row.faction))
			&& stmt->Bind(2, row.score)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] UpsertScore failed zid={} fac={}",
				row.zoneId, static_cast<unsigned>(row.faction));
		return ok;
	}
}
