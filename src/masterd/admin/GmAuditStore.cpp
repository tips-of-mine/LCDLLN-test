// ADM.0 — Journal d'audit des commandes de jeu : voir GmAuditStore.h.

#include "src/masterd/admin/GmAuditStore.h"

#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

namespace engine::server
{
	bool InsertGmAudit(db::ConnectionPool& pool, const GmAuditRecord& record)
	{
		if (!pool.IsInitialized())
			return false;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return false;
		auto* ins = cache->Acquire(mysql,
			"INSERT INTO gm_command_audit (created_at, account_id, role, command, args, result, target_name) "
			"VALUES (UTC_TIMESTAMP(3), ?, ?, ?, ?, ?, NULLIF(?, ''))");
		const std::string role = engine::admin::TruncateUtf8(record.role, 16);
		const std::string command = engine::admin::TruncateUtf8(record.command, 48);
		const std::string args = engine::admin::TruncateUtf8(record.args, 255);
		const std::string result = engine::admin::TruncateUtf8(record.result, 16);
		const std::string target = engine::admin::TruncateUtf8(record.targetName, 64);
		if (ins == nullptr || !ins->Bind(0, record.accountId) || !ins->Bind(1, std::string_view(role))
			|| !ins->Bind(2, std::string_view(command)) || !ins->Bind(3, std::string_view(args))
			|| !ins->Bind(4, std::string_view(result)) || !ins->Bind(5, std::string_view(target)) || !ins->Execute())
		{
			LOG_WARN(Db, "[GmAuditStore] ADM.0 écriture d'audit impossible (account={}, cmd=/{}) : {}",
				record.accountId, record.command, ins != nullptr ? ins->LastError() : std::string("statement"));
			return false;
		}
		return true;
	}

	bool ListGmAudit(db::ConnectionPool& pool, std::string_view filter, size_t limit, std::vector<GmAuditRecord>& out)
	{
		out.clear();
		if (!pool.IsInitialized())
			return false;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return false;
		// Filtre vide : « ? = '' » vrai, toutes les lignes.
		auto* sel = cache->Acquire(mysql,
			"SELECT DATE_FORMAT(g.created_at, '%Y-%m-%d %H:%i:%s'), g.account_id, g.role, g.command, g.args, g.result, "
			"COALESCE(g.target_name, ''), COALESCE(a.login, '') "
			"FROM gm_command_audit g LEFT JOIN accounts a ON a.id = g.account_id "
			"WHERE (? = '' OR a.login = ? OR g.target_name = ?) "
			"ORDER BY g.id DESC LIMIT ?");
		const std::string f(engine::admin::Trim(filter));
		if (sel == nullptr || !sel->Bind(0, std::string_view(f)) || !sel->Bind(1, std::string_view(f))
			|| !sel->Bind(2, std::string_view(f)) || !sel->Bind(3, static_cast<uint64_t>(limit)) || !sel->Execute())
		{
			LOG_WARN(Db, "[GmAuditStore] ADM.0 lecture d'audit impossible : {}",
				sel != nullptr ? sel->LastError() : std::string("statement"));
			return false;
		}
		while (sel->FetchRow())
		{
			GmAuditRecord r;
			r.createdAtUtc = sel->GetString(0);
			r.accountId = sel->GetUInt64(1);
			r.role = sel->GetString(2);
			r.command = sel->GetString(3);
			r.args = sel->GetString(4);
			r.result = sel->GetString(5);
			r.targetName = sel->GetString(6);
			r.login = sel->GetString(7);
			out.push_back(std::move(r));
		}
		return true;
	}
}
