// Wave 5 Persistence (Phase 5.21b) - Implementation MysqlArenaStore.
// N1-F : converti en prepared statements (1 SELECT + 1 INSERT upsert).

#include "src/masterd/arena/MysqlArenaStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::arena
{
	bool MysqlArenaStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<ArenaTeamRow> MysqlArenaStore::LoadForAccount(uint64_t accountId) const
	{
		std::vector<ArenaTeamRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT team_id, account_id_owner, name, size, rating, "
			"weekly_games, weekly_wins, season_games, season_wins "
			"FROM arena_teams WHERE account_id_owner = ? ORDER BY team_id ASC");
		if (!stmt || !stmt->Bind(0, accountId) || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlArenaStore] LoadForAccount query failed account={}", accountId);
			return out;
		}
		while (stmt->FetchRow())
		{
			ArenaTeamRow r;
			r.teamId         = static_cast<uint32_t>(stmt->GetUInt64(0));
			r.accountIdOwner = stmt->GetUInt64(1);
			r.name           = stmt->GetString(2);
			r.size           = static_cast<uint8_t>(stmt->GetUInt64(3));
			r.rating         = static_cast<uint32_t>(stmt->GetUInt64(4));
			r.weeklyGames    = static_cast<uint32_t>(stmt->GetUInt64(5));
			r.weeklyWins     = static_cast<uint32_t>(stmt->GetUInt64(6));
			r.seasonGames    = static_cast<uint32_t>(stmt->GetUInt64(7));
			r.seasonWins     = static_cast<uint32_t>(stmt->GetUInt64(8));
			out.push_back(std::move(r));
		}
		return out;
	}

	bool MysqlArenaStore::Upsert(const ArenaTeamRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO arena_teams ("
			"team_id, account_id_owner, name, size, rating, "
			"weekly_games, weekly_wins, season_games, season_wins"
			") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
			"ON DUPLICATE KEY UPDATE "
			"name = VALUES(name), size = VALUES(size), rating = VALUES(rating), "
			"weekly_games = VALUES(weekly_games), weekly_wins = VALUES(weekly_wins), "
			"season_games = VALUES(season_games), season_wins = VALUES(season_wins)");
		const bool ok = stmt
			&& stmt->Bind(0, row.teamId)
			&& stmt->Bind(1, row.accountIdOwner)
			&& stmt->Bind(2, std::string_view(row.name))
			&& stmt->Bind(3, static_cast<uint32_t>(row.size))
			&& stmt->Bind(4, row.rating)
			&& stmt->Bind(5, row.weeklyGames)
			&& stmt->Bind(6, row.weeklyWins)
			&& stmt->Bind(7, row.seasonGames)
			&& stmt->Bind(8, row.seasonWins)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlArenaStore] Upsert failed team={} account={}",
				row.teamId, row.accountIdOwner);
		return ok;
	}
}
