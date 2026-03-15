// M21.6 — Helpers Execute/Query and explicit transactions for raw SQL access.

#include "engine/server/db/DbHelpers.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <chrono>
#include <mutex>
#include <string>

namespace engine::server::db
{
	namespace
	{
		std::mutex g_latencyObserverMutex;
		std::function<void(int)> g_latencyObserver;

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

	void SetDbLatencyObserver(std::function<void(int)> observer)
	{
		std::lock_guard<std::mutex> lock(g_latencyObserverMutex);
		g_latencyObserver = std::move(observer);
	}

	bool DbExecute(MYSQL* mysql, std::string_view sql)
	{
		if (!mysql || sql.empty())
			return false;
		auto start = std::chrono::steady_clock::now();
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
		auto end = std::chrono::steady_clock::now();
		int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
		{
			std::lock_guard<std::mutex> lock(g_latencyObserverMutex);
			if (g_latencyObserver)
				g_latencyObserver(ms);
		}
		return true;
	}

	MYSQL_RES* DbQuery(MYSQL* mysql, std::string_view sql)
	{
		if (!mysql || sql.empty())
			return nullptr;
		auto start = std::chrono::steady_clock::now();
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
		auto end = std::chrono::steady_clock::now();
		int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
		{
			std::lock_guard<std::mutex> lock(g_latencyObserverMutex);
			if (g_latencyObserver)
				g_latencyObserver(ms);
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
