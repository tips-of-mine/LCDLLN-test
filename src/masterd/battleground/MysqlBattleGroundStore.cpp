// Wave 5 Persistence (Phase 5.10b) - Implementation MysqlBattleGroundStore.
// N1-F : converti en prepared statements (1 INSERT + 1 SELECT, LIMIT bindé).

#include "src/masterd/battleground/MysqlBattleGroundStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::bg_db
{
	bool MysqlBattleGroundStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	uint64_t MysqlBattleGroundStore::InsertMatch(uint16_t bgType, std::string_view mapName,
		uint32_t allianceScore, uint32_t hordeScore,
		uint8_t winnerFaction, uint32_t durationSec,
		uint64_t startedAtUnixMs)
	{
		if (!IsAvailable()) return 0u;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0u;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO bg_match_history ("
			"bg_type, map_name, alliance_score, horde_score, "
			"winner_faction, duration_sec, started_at_unix_ms"
			") VALUES (?, ?, ?, ?, ?, ?, ?)");
		const bool ok = stmt
			&& stmt->Bind(0, static_cast<uint32_t>(bgType))
			&& stmt->Bind(1, mapName)
			&& stmt->Bind(2, allianceScore)
			&& stmt->Bind(3, hordeScore)
			&& stmt->Bind(4, static_cast<uint32_t>(winnerFaction))
			&& stmt->Bind(5, durationSec)
			&& stmt->Bind(6, startedAtUnixMs)
			&& stmt->Execute();
		if (!ok)
		{
			LOG_WARN(Net, "[MysqlBattleGroundStore] InsertMatch failed bgType={} winner={}",
				static_cast<unsigned>(bgType), static_cast<unsigned>(winnerFaction));
			return 0u;
		}
		const uint64_t newId = mysql_insert_id(mysql);
		LOG_DEBUG(Net, "[MysqlBattleGroundStore] InsertMatch ok match_id={} bgType={} dur={}s",
			newId, static_cast<unsigned>(bgType), durationSec);
		return newId;
	}

	std::vector<MatchHistoryRow> MysqlBattleGroundStore::LoadRecent(size_t limit) const
	{
		std::vector<MatchHistoryRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		// Clamp limit pour eviter une requete monstre par accident.
		if (limit == 0u) limit = 1u;
		if (limit > 1000u) limit = 1000u;

		// LIMIT ? est supporté depuis MySQL 5.7 sur les prepared statements.
		auto* stmt = cache->Acquire(mysql,
			"SELECT match_id, bg_type, map_name, alliance_score, "
			"horde_score, winner_faction, duration_sec, started_at_unix_ms "
			"FROM bg_match_history ORDER BY started_at_unix_ms DESC LIMIT ?");
		if (!stmt || !stmt->Bind(0, static_cast<uint64_t>(limit)) || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlBattleGroundStore] LoadRecent query failed");
			return out;
		}
		while (stmt->FetchRow())
		{
			MatchHistoryRow r;
			r.matchId          = stmt->GetUInt64(0);
			r.bgType           = static_cast<uint16_t>(stmt->GetUInt64(1));
			r.mapName          = stmt->GetString(2);
			r.allianceScore    = static_cast<uint32_t>(stmt->GetUInt64(3));
			r.hordeScore       = static_cast<uint32_t>(stmt->GetUInt64(4));
			r.winnerFaction    = static_cast<uint8_t>(stmt->GetUInt64(5));
			r.durationSec      = static_cast<uint32_t>(stmt->GetUInt64(6));
			r.startedAtUnixMs  = stmt->GetUInt64(7);
			out.push_back(std::move(r));
		}
		LOG_INFO(Net, "[MysqlBattleGroundStore] LoadRecent loaded {} rows (limit={})", out.size(), limit);
		return out;
	}
}
