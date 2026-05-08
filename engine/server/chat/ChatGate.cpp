#include "engine/server/chat/ChatGate.h"

#include "engine/core/Log.h"
#include "engine/server/AccountRecord.h"
#include "engine/server/AccountStore.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"

#if defined(__unix__) || defined(__APPLE__)
#include <mysql.h>
#endif

#include <cstdlib>
#include <cstring>

namespace engine::server::chat
{
	ChatGate::ChatGate(ChatGateConfig cfg) : m_cfg(std::move(cfg)) {}

	void ChatGate::SetMuteLookup(ChatMuteLookupFn fn)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_muteLookup = std::move(fn);
	}

	void ChatGate::SetBannedCheck(AccountBannedFn fn)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_bannedCheck = std::move(fn);
	}

	void ChatGate::ResetState()
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_windows.clear();
	}

	void ChatGate::Reconfigure(ChatGateConfig cfg)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_cfg = std::move(cfg);
		m_windows.clear();
	}

	void ChatGate::PurgeWindow(WindowState& w, uint64_t nowMs) const
	{
		// `nowMs - floodWindowMs` peut underflow si la fenêtre est plus
		// large que le now (quasi impossible en prod : nowMs est l'epoch
		// ms, ~1.7e12 actuellement). On s'en protège quand même.
		const uint64_t cutoff = (nowMs > m_cfg.floodWindowMs)
			? (nowMs - m_cfg.floodWindowMs) : 0ULL;
		while (!w.tsMs.empty() && w.tsMs.front() < cutoff)
			w.tsMs.pop_front();
	}

	ChatGateResult ChatGate::Decide(uint64_t accountId, uint64_t nowMs) const
	{
		ChatGateResult r;

		// Snapshot des callbacks sous lock — on les copie pour pouvoir
		// les invoquer hors-lock (les callbacks DB peuvent prendre du
		// temps et on ne veut pas bloquer toutes les autres décisions).
		ChatMuteLookupFn muteFn;
		AccountBannedFn  banFn;
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			muteFn = m_muteLookup;
			banFn  = m_bannedCheck;
		}

		// 1. Banni : court-circuit (priorité la plus haute).
		if (banFn && banFn(accountId))
		{
			r.decision = ChatGateDecision::Banned;
			return r;
		}

		// 2. Mute : si présent ET non expiré.
		if (muteFn)
		{
			if (auto m = muteFn(accountId))
			{
				const bool permanent = (m->untilTsMs == 0);
				if (permanent || m->untilTsMs > nowMs)
				{
					r.decision  = ChatGateDecision::Muted;
					r.reason    = std::move(m->reason);
					r.untilTsMs = m->untilTsMs;
					return r;
				}
			}
		}

		// 3. Anti-flood : sliding window. On lit l'état SANS le modifier
		// (Decide est const). DecideAndRecord, en revanche, met à jour.
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			auto it = m_windows.find(accountId);
			if (it != m_windows.end())
			{
				WindowState copy = it->second;
				PurgeWindow(copy, nowMs);
				if (copy.tsMs.size() >= m_cfg.floodMaxMessages)
				{
					r.decision = ChatGateDecision::Flooding;
					return r;
				}
			}
		}

		r.decision = ChatGateDecision::Allowed;
		return r;
	}

	ChatGateResult ChatGate::DecideAndRecord(uint64_t accountId, uint64_t nowMs)
	{
		// On effectue la même séquence que Decide, mais en gardant le
		// lock anti-flood pour la mise à jour atomique check+record.

		ChatMuteLookupFn muteFn;
		AccountBannedFn  banFn;
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			muteFn = m_muteLookup;
			banFn  = m_bannedCheck;
		}

		ChatGateResult r;

		if (banFn && banFn(accountId))
		{
			r.decision = ChatGateDecision::Banned;
			return r;
		}

		if (muteFn)
		{
			if (auto m = muteFn(accountId))
			{
				const bool permanent = (m->untilTsMs == 0);
				if (permanent || m->untilTsMs > nowMs)
				{
					r.decision  = ChatGateDecision::Muted;
					r.reason    = std::move(m->reason);
					r.untilTsMs = m->untilTsMs;
					return r;
				}
			}
		}

		std::lock_guard<std::mutex> lk(m_mutex);

		// Cap soft de la table (on n'évince pas activement ; on refuse
		// d'ajouter une nouvelle entrée si on a déjà plein de comptes
		// actifs trackés). En pratique avec maxTrackedAccounts = 4096 et
		// floodWindowMs = 5000 ms, ça couvre largement le besoin réel.
		auto it = m_windows.find(accountId);
		if (it == m_windows.end())
		{
			if (m_cfg.maxTrackedAccounts > 0 && m_windows.size() >= m_cfg.maxTrackedAccounts)
			{
				// Full : on refuse pour ne pas grossir indéfiniment.
				// Comportement conservateur : on autorise quand même le
				// message (on ne pénalise pas un nouvel account juste
				// parce que la table est pleine), mais on logge un warn.
				LOG_WARN(Core, "[ChatGate] tracked accounts cap reached ({}); skipping flood window for new account {}",
					m_cfg.maxTrackedAccounts, accountId);
				r.decision = ChatGateDecision::Allowed;
				return r;
			}
			it = m_windows.emplace(accountId, WindowState{}).first;
		}

		WindowState& w = it->second;
		PurgeWindow(w, nowMs);
		if (w.tsMs.size() >= m_cfg.floodMaxMessages)
		{
			r.decision = ChatGateDecision::Flooding;
			return r;
		}

		w.tsMs.push_back(nowMs);
		r.decision = ChatGateDecision::Allowed;
		return r;
	}

	void ChatGate::WireProduction(engine::server::db::ConnectionPool* pool, AccountStore* accounts)
	{
		SetMuteLookup(MakeSqlMuteLookup(pool));
		SetBannedCheck(MakeAccountStoreBannedCheck(accounts));
	}

	ChatMuteLookupFn MakeSqlMuteLookup(engine::server::db::ConnectionPool* pool)
	{
#if defined(__unix__) || defined(__APPLE__)
		return [pool](uint64_t accountId) -> std::optional<ChatMute>
		{
			if (!pool || !pool->IsInitialized())
				return std::nullopt;

			auto guard = pool->Acquire();
			MYSQL* mysql = guard.get();
			if (!mysql)
				return std::nullopt;

			char sql[256];
			std::snprintf(sql, sizeof(sql),
				"SELECT until_ts, reason FROM chat_mutes WHERE account_id = %llu LIMIT 1",
				static_cast<unsigned long long>(accountId));

			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
				return std::nullopt;

			std::optional<ChatMute> out;
			if (MYSQL_ROW row = mysql_fetch_row(res))
			{
				ChatMute m;
				m.untilTsMs = row[0] ? std::strtoull(row[0], nullptr, 10) : 0ULL;
				m.reason    = row[1] ? row[1] : "";
				out = std::move(m);
			}
			mysql_free_result(res);
			return out;
		};
#else
		(void)pool;
		// Sur Windows/build sans MySQL : pas de DB, jamais muté.
		return [](uint64_t) -> std::optional<ChatMute> { return std::nullopt; };
#endif
	}

	AccountBannedFn MakeAccountStoreBannedCheck(AccountStore* accounts)
	{
		return [accounts](uint64_t accountId) -> bool
		{
			if (!accounts)
				return false;
			auto rec = accounts->FindByAccountId(accountId);
			if (!rec)
				return false;
			return rec->status == AccountStatus::Locked;
		};
	}
}
