#include "src/masterd/gmtickets/MysqlGmTicketStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace engine::server::gmtickets
{
	namespace
	{
		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}

		GmTicket RowToTicket(MYSQL_ROW row)
		{
			GmTicket t;
			if (row[0]) t.id           = std::strtoull(row[0], nullptr, 10);
			if (row[1]) t.reporter     = std::strtoull(row[1], nullptr, 10);
			if (row[2]) t.body         = row[2];
			if (row[3]) t.createdTsMs  = std::strtoull(row[3], nullptr, 10);
			if (row[4]) t.resolvedTsMs = std::strtoull(row[4], nullptr, 10);
			if (row[5]) t.assignedGm   = std::strtoull(row[5], nullptr, 10);
			if (row[6])
			{
				const auto st = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
				t.state = static_cast<TicketState>(std::min<uint32_t>(st, 3));
			}
			return t;
		}
	}

	uint64_t MysqlGmTicketStore::Insert(GmTicket& out)
	{
		if (!m_pool || !m_pool->IsInitialized()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return 0;

		const std::string body = EscapeMysql(mysql, out.body);
		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO gm_tickets (reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state) "
			"VALUES (%llu, '%s', %llu, %llu, %llu, %u)",
			static_cast<unsigned long long>(out.reporter),
			body.c_str(),
			static_cast<unsigned long long>(out.createdTsMs),
			static_cast<unsigned long long>(out.resolvedTsMs),
			static_cast<unsigned long long>(out.assignedGm),
			static_cast<unsigned>(out.state));

		if (!engine::server::db::DbExecute(mysql, sql))
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
		if (!mysql) return false;

		const std::string body = EscapeMysql(mysql, t.body);
		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"UPDATE gm_tickets SET body = '%s', resolved_ts_ms = %llu, "
			"assigned_gm = %llu, state = %u WHERE ticket_id = %llu",
			body.c_str(),
			static_cast<unsigned long long>(t.resolvedTsMs),
			static_cast<unsigned long long>(t.assignedGm),
			static_cast<unsigned>(t.state),
			static_cast<unsigned long long>(t.id));
		return engine::server::db::DbExecute(mysql, sql);
	}

	bool MysqlGmTicketStore::Delete(TicketId id)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;
		char sql[128];
		std::snprintf(sql, sizeof(sql),
			"DELETE FROM gm_tickets WHERE ticket_id = %llu",
			static_cast<unsigned long long>(id));
		return engine::server::db::DbExecute(mysql, sql);
	}

	std::optional<GmTicket> MysqlGmTicketStore::Find(TicketId id) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return std::nullopt;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT ticket_id, reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state "
			"FROM gm_tickets WHERE ticket_id = %llu LIMIT 1",
			static_cast<unsigned long long>(id));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return std::nullopt;
		std::optional<GmTicket> out;
		if (MYSQL_ROW row = mysql_fetch_row(res))
			out = RowToTicket(row);
		engine::server::db::DbFreeResult(res);
		return out;
	}

	std::vector<GmTicket> MysqlGmTicketStore::ListOpen() const
	{
		std::vector<GmTicket> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		const char* sql =
			"SELECT ticket_id, reporter_account_id, body, created_ts_ms, "
			"resolved_ts_ms, assigned_gm, state "
			"FROM gm_tickets WHERE state = 0 ORDER BY created_ts_ms ASC";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
			out.push_back(RowToTicket(row));
		engine::server::db::DbFreeResult(res);
		return out;
	}
}
