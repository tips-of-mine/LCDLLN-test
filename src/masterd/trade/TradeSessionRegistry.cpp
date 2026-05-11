// CMANGOS.27 (Phase 4.27 step 3+4) -- Implementation TradeSessionRegistry.

#include "src/masterd/trade/TradeSessionRegistry.h"

namespace engine::server::trade
{
	SessionId TradeSessionRegistry::Begin(uint64_t accountA, uint64_t accountB)
	{
		// Refuse selfTrade : la verification est aussi cote handler (pour
		// retourner SelfTrade comme code d'erreur), mais on protege ici aussi.
		if (accountA == 0u || accountB == 0u || accountA == accountB)
			return 0u;
		// Refuse si l'un des 2 est deja dans une trade.
		if (m_byAccount.count(accountA) != 0u || m_byAccount.count(accountB) != 0u)
			return 0u;

		const SessionId sid = m_next++;
		m_sessions.emplace(sid,
			std::make_unique<engine::server::trade::TradeSession>(accountA, accountB));
		m_byAccount.emplace(accountA, sid);
		m_byAccount.emplace(accountB, sid);
		return sid;
	}

	engine::server::trade::TradeSession* TradeSessionRegistry::GetById(SessionId sid)
	{
		auto it = m_sessions.find(sid);
		if (it == m_sessions.end()) return nullptr;
		return it->second.get();
	}

	engine::server::trade::TradeSession* TradeSessionRegistry::GetByAccount(uint64_t accountId)
	{
		auto itAcc = m_byAccount.find(accountId);
		if (itAcc == m_byAccount.end()) return nullptr;
		return GetById(itAcc->second);
	}

	bool TradeSessionRegistry::IsInTrade(uint64_t accountId) const
	{
		return m_byAccount.count(accountId) != 0u;
	}

	SessionId TradeSessionRegistry::GetSessionByAccount(uint64_t accountId) const
	{
		auto it = m_byAccount.find(accountId);
		if (it == m_byAccount.end()) return 0u;
		return it->second;
	}

	void TradeSessionRegistry::End(SessionId sid)
	{
		auto itSess = m_sessions.find(sid);
		if (itSess == m_sessions.end()) return;
		// Retire les 2 entrees byAccount avant de detruire la session.
		const uint64_t a = itSess->second->PlayerA();
		const uint64_t b = itSess->second->PlayerB();
		m_byAccount.erase(a);
		m_byAccount.erase(b);
		m_sessions.erase(itSess);
	}
}
