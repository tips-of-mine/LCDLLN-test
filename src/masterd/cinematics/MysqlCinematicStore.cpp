// Wave 11 Persistence Cinematics — Implementation MysqlCinematicStore.

#include "src/masterd/cinematics/MysqlCinematicStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

namespace engine::server::cinematics
{
	namespace
	{
		// Pas de chaine a echapper pour cette table (cles numeriques uniquement),
		// donc on omet EscapeMysql ici. Pattern Wave 5 garde la helper en
		// anonymous namespace quand elle est utile ; on respecte la convention
		// par absence quand on n'en a pas besoin.
	}

	bool MysqlCinematicStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	bool MysqlCinematicStore::MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		// INSERT IGNORE : idempotent vis-a-vis de la PK (account_id, sequence_id).
		// Si la ligne existe deja, on garde le first_seen_ts_ms d'origine.
		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT IGNORE INTO cinematic_seen (account_id, sequence_id, first_seen_ts_ms) "
			"VALUES (%llu, %u, %llu)",
			static_cast<unsigned long long>(accountId),
			sequenceId,
			static_cast<unsigned long long>(nowMs));

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
		{
			LOG_WARN(Net, "[MysqlCinematicStore] MarkSeen failed account={} sequenceId={}",
				accountId, sequenceId);
		}
		return ok;
	}

	bool MysqlCinematicStore::HasSeen(uint64_t accountId, uint32_t sequenceId) const
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT 1 FROM cinematic_seen WHERE account_id = %llu AND sequence_id = %u LIMIT 1",
			static_cast<unsigned long long>(accountId),
			sequenceId);

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlCinematicStore] HasSeen query failed account={} sequenceId={}",
				accountId, sequenceId);
			return false;
		}
		const bool found = (mysql_fetch_row(res) != nullptr);
		engine::server::db::DbFreeResult(res);
		return found;
	}

	std::vector<uint32_t> MysqlCinematicStore::ListSeen(uint64_t accountId) const
	{
		std::vector<uint32_t> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT sequence_id FROM cinematic_seen WHERE account_id = %llu",
			static_cast<unsigned long long>(accountId));

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlCinematicStore] ListSeen query failed account={}", accountId);
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			if (row[0])
				out.push_back(static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10)));
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}
}
