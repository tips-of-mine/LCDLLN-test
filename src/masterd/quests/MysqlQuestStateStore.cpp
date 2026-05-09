#include "engine/server/quests/MysqlQuestStateStore.h"

#include "engine/core/Log.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"

#include <mysql.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace engine::server::quests
{
	bool MysqlQuestStateStore::Upsert(AccountId accountId, QuestId questId, QuestStatus status)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO account_quest_state (account_id, quest_id, status) "
			"VALUES (%llu, %u, %u) "
			"ON DUPLICATE KEY UPDATE status = VALUES(status)",
			static_cast<unsigned long long>(accountId),
			questId,
			static_cast<unsigned>(status));
		return engine::server::db::DbExecute(mysql, sql);
	}

	bool MysqlQuestStateStore::Delete(AccountId accountId, QuestId questId)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;
		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"DELETE FROM account_quest_state WHERE account_id = %llu AND quest_id = %u",
			static_cast<unsigned long long>(accountId),
			questId);
		return engine::server::db::DbExecute(mysql, sql);
	}

	std::vector<QuestStateRow> MysqlQuestStateStore::Load(AccountId accountId) const
	{
		std::vector<QuestStateRow> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT account_id, quest_id, status FROM account_quest_state "
			"WHERE account_id = %llu",
			static_cast<unsigned long long>(accountId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			QuestStateRow r;
			r.accountId = row[0] ? std::strtoull(row[0], nullptr, 10) : 0;
			r.questId   = row[1] ? static_cast<QuestId>(std::strtoul(row[1], nullptr, 10)) : 0;
			const auto st = row[2] ? static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10)) : 0u;
			r.status    = static_cast<QuestStatus>(std::min<uint32_t>(st, 5u));
			out.push_back(r);
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}
}
