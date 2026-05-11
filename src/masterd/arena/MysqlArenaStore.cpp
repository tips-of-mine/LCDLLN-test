// Wave 5 Persistence (Phase 5.21b) - Implementation MysqlArenaStore.

#include "src/masterd/arena/MysqlArenaStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace engine::server::arena
{
	namespace
	{
		std::string EscapeMysql(MYSQL* mysql, const std::string& v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}
	}

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
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT team_id, account_id_owner, name, size, rating, "
			"weekly_games, weekly_wins, season_games, season_wins "
			"FROM arena_teams WHERE account_id_owner = %llu ORDER BY team_id ASC",
			static_cast<unsigned long long>(accountId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlArenaStore] LoadForAccount query failed account={}", accountId);
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			ArenaTeamRow r;
			if (row[0]) r.teamId         = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			if (row[1]) r.accountIdOwner = std::strtoull(row[1], nullptr, 10);
			if (row[2]) r.name           = row[2];
			if (row[3]) r.size           = static_cast<uint8_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4]) r.rating         = static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10));
			if (row[5]) r.weeklyGames    = static_cast<uint32_t>(std::strtoul(row[5], nullptr, 10));
			if (row[6]) r.weeklyWins     = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
			if (row[7]) r.seasonGames    = static_cast<uint32_t>(std::strtoul(row[7], nullptr, 10));
			if (row[8]) r.seasonWins     = static_cast<uint32_t>(std::strtoul(row[8], nullptr, 10));
			out.push_back(std::move(r));
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}

	bool MysqlArenaStore::Upsert(const ArenaTeamRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		const std::string name = EscapeMysql(mysql, row.name);

		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO arena_teams ("
			"team_id, account_id_owner, name, size, rating, "
			"weekly_games, weekly_wins, season_games, season_wins"
			") VALUES (%u, %llu, '%s', %u, %u, %u, %u, %u, %u) "
			"ON DUPLICATE KEY UPDATE "
			"name = VALUES(name), size = VALUES(size), rating = VALUES(rating), "
			"weekly_games = VALUES(weekly_games), weekly_wins = VALUES(weekly_wins), "
			"season_games = VALUES(season_games), season_wins = VALUES(season_wins)",
			row.teamId,
			static_cast<unsigned long long>(row.accountIdOwner),
			name.c_str(),
			static_cast<unsigned>(row.size),
			row.rating,
			row.weeklyGames, row.weeklyWins,
			row.seasonGames, row.seasonWins);

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlArenaStore] Upsert failed team={} account={}",
				row.teamId, row.accountIdOwner);
		return ok;
	}
}
