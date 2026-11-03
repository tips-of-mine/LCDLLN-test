// ADM.8 — Accès base des commandes de modération : voir GmModerationStore.h.

#include "src/masterd/admin/GmModerationStore.h"

#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

namespace engine::server
{
	int DeleteChatMute(db::ConnectionPool& pool, uint64_t accountId)
	{
		if (!pool.IsInitialized())
			return -1;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return -1;
		auto* del = cache->Acquire(mysql, "DELETE FROM chat_mutes WHERE account_id = ?");
		if (del == nullptr || !del->Bind(0, accountId) || !del->Execute())
		{
			LOG_WARN(Db, "[GmModerationStore] ADM.8 levée de sanction impossible (account={}) : {}",
				accountId, del != nullptr ? del->LastError() : std::string("statement"));
			return -1;
		}
		return static_cast<int>(del->AffectedRows());
	}

	bool ListActiveMutes(db::ConnectionPool& pool, uint64_t nowMs, size_t limit, std::vector<GmMuteRow>& out)
	{
		out.clear();
		if (!pool.IsInitialized())
			return false;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return false;
		auto* sel = cache->Acquire(mysql,
			"SELECT m.account_id, COALESCE(a.login, ''), m.until_ts, m.reason FROM chat_mutes m "
			"LEFT JOIN accounts a ON a.id = m.account_id "
			"WHERE m.until_ts = 0 OR m.until_ts > ? ORDER BY m.until_ts DESC LIMIT ?");
		if (sel == nullptr || !sel->Bind(0, nowMs) || !sel->Bind(1, static_cast<uint64_t>(limit)) || !sel->Execute())
			return false;
		while (sel->FetchRow())
		{
			GmMuteRow r;
			r.accountId = sel->GetUInt64(0);
			r.login = sel->GetString(1);
			r.untilTsMs = sel->GetUInt64(2);
			r.reason = sel->GetString(3);
			out.push_back(std::move(r));
		}
		return true;
	}

	bool ListLockedAccounts(db::ConnectionPool& pool, std::string_view filter, size_t limit, std::vector<std::string>& out)
	{
		out.clear();
		if (!pool.IsInitialized())
			return false;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return false;
		auto* sel = cache->Acquire(mysql,
			"SELECT login FROM accounts WHERE account_status IN (1, 2) AND (? = '' OR login LIKE CONCAT('%', ?, '%')) "
			"ORDER BY login LIMIT ?");
		const std::string f(engine::admin::Trim(filter));
		if (sel == nullptr || !sel->Bind(0, std::string_view(f)) || !sel->Bind(1, std::string_view(f))
			|| !sel->Bind(2, static_cast<uint64_t>(limit)) || !sel->Execute())
			return false;
		while (sel->FetchRow())
			out.push_back(sel->GetString(0));
		return true;
	}
}
