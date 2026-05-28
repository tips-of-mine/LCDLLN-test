// N1-E : converti en prepared statements (1 INSERT upsert + 1 SELECT).

#include "src/masterd/reputation/MysqlReputationStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::reputation
{
	bool MysqlReputationStore::Upsert(AccountId accountId, FactionId factionId, int32_t value)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		// MySQL upsert : INSERT ... ON DUPLICATE KEY UPDATE.
		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO account_reputation (account_id, faction_id, value) "
			"VALUES (?, ?, ?) "
			"ON DUPLICATE KEY UPDATE value = VALUES(value)");
		return stmt
			&& stmt->Bind(0, static_cast<uint64_t>(accountId))
			&& stmt->Bind(1, static_cast<uint32_t>(factionId))
			&& stmt->Bind(2, value)
			&& stmt->Execute();
	}

	std::vector<ReputationRow> MysqlReputationStore::Load(AccountId accountId) const
	{
		std::vector<ReputationRow> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT account_id, faction_id, value FROM account_reputation WHERE account_id = ?");
		if (!stmt || !stmt->Bind(0, static_cast<uint64_t>(accountId)) || !stmt->Execute())
			return out;
		while (stmt->FetchRow())
		{
			ReputationRow r;
			r.accountId = stmt->GetUInt64(0);
			r.factionId = static_cast<FactionId>(stmt->GetUInt64(1));
			r.value     = stmt->GetInt32(2);
			out.push_back(r);
		}
		return out;
	}
}
