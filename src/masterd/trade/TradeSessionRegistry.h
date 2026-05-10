#pragma once
// CMANGOS.27 (Phase 4.27 step 3+4) -- TradeSessionRegistry : registry des
// TradeSession actives cote master, indexees par sessionId + reverse map
// account_id -> sessionId pour les checks d'unicite (un compte ne peut etre
// que dans 1 trade a la fois).
//
// Thread-safety : non thread-safe (appel synchrone depuis le packet handler
// thread principal du master, comme les autres handlers du repo). Si futur
// passage multi-thread, ajouter un mutex au niveau de toutes les operations.
//
// V1 : sessions transitoires (pas persistees). Au reboot, toutes les trades
// en cours sont implicitement Cancelled (les clients re-affichent une UI vide
// au prochain login). Si besoin futur de persistence (anti-griefing), creer
// une table trade_sessions avec un store associe.

#include "src/shardd/trade/TradeSession.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace engine::server::trade
{
	using SessionId = uint64_t;

	/// Registry des TradeSession actives. Une session lie 2 account_id et
	/// possede son propre sessionId monotone.
	class TradeSessionRegistry
	{
	public:
		TradeSessionRegistry() = default;

		/// Tente de creer une nouvelle session entre \p accountA et \p accountB.
		/// \return un sessionId non-zero en cas de succes, 0 si l'un des 2 est
		///         deja dans une trade (ou si accountA == accountB).
		/// Effet de bord : ajoute la session aux 2 maps internes.
		SessionId Begin(uint64_t accountA, uint64_t accountB);

		/// \return le pointeur sur la TradeSession associee a \p sid, ou nullptr.
		/// Ne transfere pas ownership ; le pointeur reste valide jusqu'a End(sid).
		engine::server::trade::TradeSession* GetById(SessionId sid);

		/// \return le pointeur sur la TradeSession ou \p accountId est partie A
		///         ou partie B, ou nullptr s'il n'est dans aucune trade.
		engine::server::trade::TradeSession* GetByAccount(uint64_t accountId);

		/// True ssi \p accountId est partie d'une trade active.
		bool IsInTrade(uint64_t accountId) const;

		/// \return le sessionId de la trade ou \p accountId est partie, ou 0.
		SessionId GetSessionByAccount(uint64_t accountId) const;

		/// Termine la session \p sid : retire des 2 maps. Pas d'erreur si \p sid
		/// est inconnu (idempotent). Apres End(), GetById(sid) retourne nullptr.
		void End(SessionId sid);

		/// Nombre de trades actives. Test/diagnostic.
		size_t Count() const { return m_sessions.size(); }

	private:
		SessionId m_next = 1;
		std::unordered_map<SessionId, std::unique_ptr<engine::server::trade::TradeSession>> m_sessions;
		/// account_id -> sessionId. Maintenu en miroir de m_sessions pour les
		/// 2 parties (A et B) de chaque trade.
		std::unordered_map<uint64_t, SessionId> m_byAccount;
	};
}
