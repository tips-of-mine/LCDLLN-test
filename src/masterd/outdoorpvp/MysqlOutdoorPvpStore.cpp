// Wave 5 Persistence (Phase 5.36b) - Implementation MysqlOutdoorPvpStore.

#include "src/masterd/outdoorpvp/MysqlOutdoorPvpStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

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
		if (!mysql) return out;

		const char* sql =
			"SELECT zone_id, objective_id, owner, capture_pct, capturing_by "
			"FROM outdoor_pvp_state";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] LoadStates query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			ObjectiveRow r;
			if (row[0]) r.zoneId      = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.objectiveId = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) r.owner       = static_cast<uint8_t>(std::strtoul(row[2], nullptr, 10));
			if (row[3]) r.capturePct  = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4]) r.capturingBy = static_cast<uint8_t>(std::strtoul(row[4], nullptr, 10));
			out.push_back(r);
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}

	std::vector<ScoreRow> MysqlOutdoorPvpStore::LoadScores() const
	{
		std::vector<ScoreRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		const char* sql = "SELECT zone_id, faction, score FROM outdoor_pvp_scores";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] LoadScores query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			ScoreRow r;
			if (row[0]) r.zoneId  = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.faction = static_cast<uint8_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) r.score   = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
			out.push_back(r);
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}

	bool MysqlOutdoorPvpStore::UpsertObjective(const ObjectiveRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO outdoor_pvp_state (zone_id, objective_id, owner, capture_pct, capturing_by) "
			"VALUES (%u, %u, %u, %u, %u) "
			"ON DUPLICATE KEY UPDATE "
			"owner = VALUES(owner), capture_pct = VALUES(capture_pct), "
			"capturing_by = VALUES(capturing_by)",
			row.zoneId, row.objectiveId,
			static_cast<unsigned>(row.owner), row.capturePct,
			static_cast<unsigned>(row.capturingBy));

		const bool ok = engine::server::db::DbExecute(mysql, sql);
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
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO outdoor_pvp_scores (zone_id, faction, score) "
			"VALUES (%u, %u, %u) "
			"ON DUPLICATE KEY UPDATE score = VALUES(score)",
			row.zoneId, static_cast<unsigned>(row.faction), row.score);

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlOutdoorPvpStore] UpsertScore failed zid={} fac={}",
				row.zoneId, static_cast<unsigned>(row.faction));
		return ok;
	}
}
