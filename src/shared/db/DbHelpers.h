#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

struct MYSQL;
struct MYSQL_RES;

namespace engine::server::db
{
	/// M23.2: Optional. Set a callback to record query latency in ms (e.g. for Prometheus db_query_latency_ms). Call from server main once.
	void SetDbLatencyObserver(std::function<void(int)> observer);

	/// Enregistre la durée depuis \a start (pour requêtes brutes hors DbExecute/DbQuery).
	void DbRecordLatencySince(std::chrono::steady_clock::time_point start);

	/// Executes a single SQL statement (or multi-statement batch). No result set expected.
	/// Returns true on success. For user-derived input use prepared statements (see docs/db_sql_guidelines.md).
	bool DbExecute(MYSQL* mysql, std::string_view sql);

	/// Executes a query and returns the first result set. Caller must call DbFreeResult on the returned pointer.
	/// Returns nullptr on error or no result. For SELECT with user-derived params use prepared statements.
	MYSQL_RES* DbQuery(MYSQL* mysql, std::string_view sql);

	/// Frees a result set returned by DbQuery.
	void DbFreeResult(MYSQL_RES* res);

	/// Starts an explicit transaction. Use for multi-table operations (see docs/db_sql_guidelines.md).
	bool DbBeginTransaction(MYSQL* mysql);
	/// Commits the current transaction.
	bool DbCommit(MYSQL* mysql);
	/// Rolls back the current transaction.
	bool DbRollback(MYSQL* mysql);

	/// RAII transaction: Begin on construction, Commit on success or Rollback on destructor if not committed.
	class ScopedTransaction
	{
	public:
		explicit ScopedTransaction(MYSQL* mysql);
		~ScopedTransaction();
		ScopedTransaction(const ScopedTransaction&) = delete;
		ScopedTransaction& operator=(const ScopedTransaction&) = delete;

		void Commit();
		bool IsCommitted() const { return m_committed; }

	private:
		MYSQL* m_mysql = nullptr;
		bool m_committed = false;
	};
}
