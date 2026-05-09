#include "src/masterd/social/MysqlIgnoreStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

namespace engine::server::social
{
	bool MysqlIgnoreStore::Add(uint64_t owner, uint64_t target)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		// INSERT IGNORE : si la PK (owner, target) existe deja, no-op.
		// Le cap de 50 ignored est applique cote IgnoreListManager runtime.
		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT IGNORE INTO account_ignore_list (owner_account_id, target_account_id) "
			"VALUES (%llu, %llu)",
			static_cast<unsigned long long>(owner),
			static_cast<unsigned long long>(target));
		return engine::server::db::DbExecute(mysql, sql);
	}

	bool MysqlIgnoreStore::Remove(uint64_t owner, uint64_t target)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"DELETE FROM account_ignore_list "
			"WHERE owner_account_id = %llu AND target_account_id = %llu",
			static_cast<unsigned long long>(owner),
			static_cast<unsigned long long>(target));
		return engine::server::db::DbExecute(mysql, sql);
	}

	bool MysqlIgnoreStore::IsIgnored(uint64_t owner, uint64_t target) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT 1 FROM account_ignore_list "
			"WHERE owner_account_id = %llu AND target_account_id = %llu LIMIT 1",
			static_cast<unsigned long long>(owner),
			static_cast<unsigned long long>(target));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return false;
		const bool found = (mysql_fetch_row(res) != nullptr);
		engine::server::db::DbFreeResult(res);
		return found;
	}

	std::vector<uint64_t> MysqlIgnoreStore::List(uint64_t owner) const
	{
		std::vector<uint64_t> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT target_account_id FROM account_ignore_list "
			"WHERE owner_account_id = %llu ORDER BY target_account_id ASC",
			static_cast<unsigned long long>(owner));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			if (row[0]) out.push_back(std::strtoull(row[0], nullptr, 10));
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}

	size_t MysqlIgnoreStore::Size(uint64_t owner) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return 0;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT COUNT(*) FROM account_ignore_list WHERE owner_account_id = %llu",
			static_cast<unsigned long long>(owner));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return 0;
		size_t count = 0;
		if (MYSQL_ROW row = mysql_fetch_row(res))
		{
			if (row[0]) count = static_cast<size_t>(std::strtoull(row[0], nullptr, 10));
		}
		engine::server::db::DbFreeResult(res);
		return count;
	}
}
