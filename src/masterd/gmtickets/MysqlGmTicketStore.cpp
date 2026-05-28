// N1-G : converti en prepared statements (Insert, Update, Delete, Find, ListOpen).

#include "src/masterd/gmtickets/MysqlGmTicketStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <algorithm>

namespace engine::server::gmtickets
{
	namespace
	{
		GmTicket StmtToTicket(engine::server::db::SqlPreparedStatement* stmt)
		{
			GmTicket t;
			t.id           = stmt->GetUInt64(0);
			t.reporter     = stmt->GetUInt64(1);
			t.body         = stmt->GetString(2);
			t.createdTsMs  = stmt->GetUInt64(3);
			t.resolvedTsMs = stmt->GetUInt64(4);
			t.assignedGm   = stmt->GetUInt64(5);
			const uint32_t st = static_cast<uint32_t>(stmt->GetUInt64(6));
			t.state = static_cast<TicketState>(std::min<uint32_t>(st, 3));
			return t;
		}
	}

	uint64_t MysqlGmTicketStore::Insert(GmTicket& out)
	{
		if (!m_pool || !m_pool->IsInitialized()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO gm_tickets (reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state) "
			"VALUES (?, ?, ?, ?, ?, ?)");
		const bool ok = stmt
			&& stmt->Bind(0, out.reporter)
			&& stmt->Bind(1, std::string_view(out.body))
			&& stmt->Bind(2, out.createdTsMs)
			&& stmt->Bind(3, out.resolvedTsMs)
			&& stmt->Bind(4, out.assignedGm)
			&& stmt->Bind(5, static_cast<uint32_t>(out.state))
			&& stmt->Execute();
		if (!ok)
		{
			LOG_ERROR(Core, "[MysqlGmTicketStore] Insert failed");
			return 0;
		}
		out.id = mysql_insert_id(mysql);
		return out.id;
	}

	bool MysqlGmTicketStore::Update(const GmTicket& t)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"UPDATE gm_tickets SET body = ?, resolved_ts_ms = ?, "
			"assigned_gm = ?, state = ? WHERE ticket_id = ?");
		return stmt
			&& stmt->Bind(0, std::string_view(t.body))
			&& stmt->Bind(1, t.resolvedTsMs)
			&& stmt->Bind(2, t.assignedGm)
			&& stmt->Bind(3, static_cast<uint32_t>(t.state))
			&& stmt->Bind(4, t.id)
			&& stmt->Execute();
	}

	bool MysqlGmTicketStore::Delete(TicketId id)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;
		auto* stmt = cache->Acquire(mysql, "DELETE FROM gm_tickets WHERE ticket_id = ?");
		return stmt && stmt->Bind(0, static_cast<uint64_t>(id)) && stmt->Execute();
	}

	std::optional<GmTicket> MysqlGmTicketStore::Find(TicketId id) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return std::nullopt;

		auto* stmt = cache->Acquire(mysql,
			"SELECT ticket_id, reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state "
			"FROM gm_tickets WHERE ticket_id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, static_cast<uint64_t>(id)) || !stmt->Execute() || !stmt->FetchRow())
			return std::nullopt;
		return StmtToTicket(stmt);
	}

	std::vector<GmTicket> MysqlGmTicketStore::ListOpen() const
	{
		std::vector<GmTicket> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT ticket_id, reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state "
			"FROM gm_tickets WHERE state = 0 ORDER BY created_ts_ms ASC");
		if (!stmt || !stmt->Execute()) return out;
		while (stmt->FetchRow())
			out.push_back(StmtToTicket(stmt));
		return out;
	}
}
