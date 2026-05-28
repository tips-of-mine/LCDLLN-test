// N1-E : converti en prepared statements (1 INSERT upsert + 1 DELETE + 1 SELECT).

#include "src/masterd/quests/MysqlQuestStateStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <algorithm>

namespace engine::server::quests
{
	bool MysqlQuestStateStore::Upsert(AccountId accountId, QuestId questId, QuestStatus status)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO account_quest_state (account_id, quest_id, status) "
			"VALUES (?, ?, ?) "
			"ON DUPLICATE KEY UPDATE status = VALUES(status)");
		return stmt
			&& stmt->Bind(0, static_cast<uint64_t>(accountId))
			&& stmt->Bind(1, static_cast<uint32_t>(questId))
			&& stmt->Bind(2, static_cast<uint32_t>(status))
			&& stmt->Execute();
	}

	bool MysqlQuestStateStore::Delete(AccountId accountId, QuestId questId)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;
		auto* stmt = cache->Acquire(mysql,
			"DELETE FROM account_quest_state WHERE account_id = ? AND quest_id = ?");
		return stmt
			&& stmt->Bind(0, static_cast<uint64_t>(accountId))
			&& stmt->Bind(1, static_cast<uint32_t>(questId))
			&& stmt->Execute();
	}

	std::vector<QuestStateRow> MysqlQuestStateStore::Load(AccountId accountId) const
	{
		std::vector<QuestStateRow> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT account_id, quest_id, status FROM account_quest_state WHERE account_id = ?");
		if (!stmt || !stmt->Bind(0, static_cast<uint64_t>(accountId)) || !stmt->Execute())
			return out;
		while (stmt->FetchRow())
		{
			QuestStateRow r;
			r.accountId = stmt->GetUInt64(0);
			r.questId   = static_cast<QuestId>(stmt->GetUInt64(1));
			const uint32_t st = static_cast<uint32_t>(stmt->GetUInt64(2));
			r.status    = static_cast<QuestStatus>(std::min<uint32_t>(st, 5u));
			out.push_back(r);
		}
		return out;
	}
}
