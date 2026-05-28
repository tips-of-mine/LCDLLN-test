// N1-F : converti en prepared statements (INSERT IGNORE + DELETE + 3 SELECTs).

#include "src/masterd/social/MysqlIgnoreStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::social
{
	bool MysqlIgnoreStore::Add(uint64_t owner, uint64_t target)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		// INSERT IGNORE : si la PK (owner, target) existe deja, no-op.
		// Le cap de 50 ignored est applique cote IgnoreListManager runtime.
		auto* stmt = cache->Acquire(mysql,
			"INSERT IGNORE INTO account_ignore_list (owner_account_id, target_account_id) VALUES (?, ?)");
		return stmt
			&& stmt->Bind(0, owner)
			&& stmt->Bind(1, target)
			&& stmt->Execute();
	}

	bool MysqlIgnoreStore::Remove(uint64_t owner, uint64_t target)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"DELETE FROM account_ignore_list "
			"WHERE owner_account_id = ? AND target_account_id = ?");
		return stmt
			&& stmt->Bind(0, owner)
			&& stmt->Bind(1, target)
			&& stmt->Execute();
	}

	bool MysqlIgnoreStore::IsIgnored(uint64_t owner, uint64_t target) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"SELECT 1 FROM account_ignore_list "
			"WHERE owner_account_id = ? AND target_account_id = ? LIMIT 1");
		if (!stmt
			|| !stmt->Bind(0, owner)
			|| !stmt->Bind(1, target)
			|| !stmt->Execute())
			return false;
		return stmt->FetchRow();
	}

	std::vector<uint64_t> MysqlIgnoreStore::List(uint64_t owner) const
	{
		std::vector<uint64_t> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT target_account_id FROM account_ignore_list "
			"WHERE owner_account_id = ? ORDER BY target_account_id ASC");
		if (!stmt || !stmt->Bind(0, owner) || !stmt->Execute())
			return out;
		while (stmt->FetchRow())
			out.push_back(stmt->GetUInt64(0));
		return out;
	}

	size_t MysqlIgnoreStore::Size(uint64_t owner) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0;

		auto* stmt = cache->Acquire(mysql,
			"SELECT COUNT(*) FROM account_ignore_list WHERE owner_account_id = ?");
		if (!stmt || !stmt->Bind(0, owner) || !stmt->Execute() || !stmt->FetchRow())
			return 0;
		return static_cast<size_t>(stmt->GetUInt64(0));
	}
}
