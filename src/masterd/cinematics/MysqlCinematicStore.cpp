// Wave 11 Persistence Cinematics — Implementation MysqlCinematicStore.
// N1-F : converti en prepared statements (1 INSERT IGNORE + 2 SELECTs).

#include "src/masterd/cinematics/MysqlCinematicStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::cinematics
{
	bool MysqlCinematicStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	bool MysqlCinematicStore::MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		// INSERT IGNORE : idempotent vis-a-vis de la PK (account_id, sequence_id).
		// Si la ligne existe deja, on garde le first_seen_ts_ms d'origine.
		auto* stmt = cache->Acquire(mysql,
			"INSERT IGNORE INTO cinematic_seen (account_id, sequence_id, first_seen_ts_ms) "
			"VALUES (?, ?, ?)");
		const bool ok = stmt
			&& stmt->Bind(0, accountId)
			&& stmt->Bind(1, sequenceId)
			&& stmt->Bind(2, nowMs)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlCinematicStore] MarkSeen failed account={} sequenceId={}",
				accountId, sequenceId);
		return ok;
	}

	bool MysqlCinematicStore::HasSeen(uint64_t accountId, uint32_t sequenceId) const
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"SELECT 1 FROM cinematic_seen WHERE account_id = ? AND sequence_id = ? LIMIT 1");
		if (!stmt
			|| !stmt->Bind(0, accountId)
			|| !stmt->Bind(1, sequenceId)
			|| !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlCinematicStore] HasSeen query failed account={} sequenceId={}",
				accountId, sequenceId);
			return false;
		}
		return stmt->FetchRow();
	}

	std::vector<uint32_t> MysqlCinematicStore::ListSeen(uint64_t accountId) const
	{
		std::vector<uint32_t> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT sequence_id FROM cinematic_seen WHERE account_id = ?");
		if (!stmt || !stmt->Bind(0, accountId) || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlCinematicStore] ListSeen query failed account={}", accountId);
			return out;
		}
		while (stmt->FetchRow())
			out.push_back(static_cast<uint32_t>(stmt->GetUInt64(0)));
		return out;
	}
}
