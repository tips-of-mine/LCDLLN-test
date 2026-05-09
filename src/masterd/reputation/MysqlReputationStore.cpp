#include "src/masterd/reputation/MysqlReputationStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

namespace engine::server::reputation
{
	bool MysqlReputationStore::Upsert(AccountId accountId, FactionId factionId, int32_t value)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		// MySQL upsert : INSERT ... ON DUPLICATE KEY UPDATE.
		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO account_reputation (account_id, faction_id, value) "
			"VALUES (%llu, %u, %d) "
			"ON DUPLICATE KEY UPDATE value = VALUES(value)",
			static_cast<unsigned long long>(accountId),
			factionId,
			value);
		return engine::server::db::DbExecute(mysql, sql);
	}

	std::vector<ReputationRow> MysqlReputationStore::Load(AccountId accountId) const
	{
		std::vector<ReputationRow> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT account_id, faction_id, value FROM account_reputation "
			"WHERE account_id = %llu",
			static_cast<unsigned long long>(accountId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			ReputationRow r;
			r.accountId = row[0] ? std::strtoull(row[0], nullptr, 10) : 0;
			r.factionId = row[1] ? static_cast<FactionId>(std::strtoul(row[1], nullptr, 10)) : 0;
			r.value     = row[2] ? std::atoi(row[2]) : 0;
			out.push_back(r);
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}
}
