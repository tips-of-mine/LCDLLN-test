// Wave 5 Persistence (Phase 5.10b) - Implementation MysqlBattleGroundStore.

#include "src/masterd/battleground/MysqlBattleGroundStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace engine::server::bg_db
{
	namespace
	{
		/// Echappe une chaine pour MySQL. Retourne vide si mysql null.
		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}
	}

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
		if (!mysql) return 0u;

		const std::string mapEsc = EscapeMysql(mysql, mapName);

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO bg_match_history ("
			"bg_type, map_name, alliance_score, horde_score, "
			"winner_faction, duration_sec, started_at_unix_ms"
			") VALUES (%u, '%s', %u, %u, %u, %u, %llu)",
			static_cast<unsigned>(bgType),
			mapEsc.c_str(),
			allianceScore,
			hordeScore,
			static_cast<unsigned>(winnerFaction),
			durationSec,
			static_cast<unsigned long long>(startedAtUnixMs));

		if (!engine::server::db::DbExecute(mysql, sql))
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
		if (!mysql) return out;

		// Clamp limit pour eviter une requete monstre par accident.
		if (limit == 0u) limit = 1u;
		if (limit > 1000u) limit = 1000u;

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"SELECT match_id, bg_type, map_name, alliance_score, "
			"horde_score, winner_faction, duration_sec, started_at_unix_ms "
			"FROM bg_match_history ORDER BY started_at_unix_ms DESC LIMIT %zu",
			limit);
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlBattleGroundStore] LoadRecent query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			MatchHistoryRow r;
			if (row[0]) r.matchId          = std::strtoull(row[0], nullptr, 10);
			if (row[1]) r.bgType           = static_cast<uint16_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) r.mapName          = row[2];
			if (row[3]) r.allianceScore    = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4]) r.hordeScore       = static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10));
			if (row[5]) r.winnerFaction    = static_cast<uint8_t>(std::strtoul(row[5], nullptr, 10));
			if (row[6]) r.durationSec      = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
			if (row[7]) r.startedAtUnixMs  = std::strtoull(row[7], nullptr, 10);
			out.push_back(std::move(r));
		}
		engine::server::db::DbFreeResult(res);
		LOG_INFO(Net, "[MysqlBattleGroundStore] LoadRecent loaded {} rows (limit={})", out.size(), limit);
		return out;
	}
}
