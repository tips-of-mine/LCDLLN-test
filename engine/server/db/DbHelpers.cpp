// M21.6 — Helpers Execute/Query and explicit transactions for raw SQL access.

#include "engine/server/db/DbHelpers.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <string>

namespace engine::server::db
{
	namespace
	{
		bool DrainResults(MYSQL* mysql)
		{
			for (;;)
			{
				MYSQL_RES* res = mysql_store_result(mysql);
				if (res)
					mysql_free_result(res);
				if (mysql_errno(mysql) != 0)
					return false;
				if (!mysql_more_results(mysql))
					break;
				if (mysql_next_result(mysql) != 0)
					return false;
			}
			return true;
		}
	}

	bool DbExecute(MYSQL* mysql, std::string_view sql)
	{
		if (!mysql || sql.empty())
			return false;
		std::string s(sql);
		if (mysql_real_query(mysql, s.c_str(), static_cast<unsigned long>(s.size())) != 0)
		{
			LOG_ERROR(Core, "[DbHelpers] Execute failed: {}", mysql_error(mysql));
			return false;
		}
		if (!DrainResults(mysql))
		{
			LOG_ERROR(Core, "[DbHelpers] DrainResults failed: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}

	MYSQL_RES* DbQuery(MYSQL* mysql, std::string_view sql)
	{
		if (!mysql || sql.empty())
			return nullptr;
		std::string s(sql);
		if (mysql_real_query(mysql, s.c_str(), static_cast<unsigned long>(s.size())) != 0)
		{
			LOG_ERROR(Core, "[DbHelpers] Query failed: {}", mysql_error(mysql));
			return nullptr;
		}
		MYSQL_RES* res = mysql_store_result(mysql);
		if (mysql_errno(mysql) != 0)
		{
			LOG_ERROR(Core, "[DbHelpers] store_result failed: {}", mysql_error(mysql));
			return nullptr;
		}
		return res;
	}

	void DbFreeResult(MYSQL_RES* res)
	{
		if (res)
			mysql_free_result(res);
	}

	bool DbBeginTransaction(MYSQL* mysql)
	{
		return DbExecute(mysql, "START TRANSACTION");
	}

	bool DbCommit(MYSQL* mysql)
	{
		return DbExecute(mysql, "COMMIT");
	}

	bool DbRollback(MYSQL* mysql)
	{
		return DbExecute(mysql, "ROLLBACK");
	}

	ScopedTransaction::ScopedTransaction(MYSQL* mysql) : m_mysql(mysql)
	{
		if (m_mysql)
			DbBeginTransaction(m_mysql);
	}

	ScopedTransaction::~ScopedTransaction()
	{
		if (m_mysql && !m_committed)
			DbRollback(m_mysql);
	}

	void ScopedTransaction::Commit()
	{
		if (m_mysql && !m_committed)
		{
			DbCommit(m_mysql);
			m_committed = true;
		}
	}
}
