#pragma once
// CMANGOS.27 (Phase 4.27a) — TradeSession : echange direct entre 2 joueurs
// avec gates de confirmation (offer -> lock -> mutual confirm -> commit).
// Header-only. Ne touche pas a l'inventaire reel : retourne le delta validee
// au caller pour application atomique cote shard.

#include <cstdint>
#include <vector>
#include <utility>

namespace engine::server::trade
{
	using PlayerId = uint64_t;
	using ItemGuid = uint64_t;

	enum class TradeState : uint8_t
	{
		Open,        ///< les 2 joueurs editent l'offer
		LockedA,     ///< A a verrouille, B peut encore editer
		LockedB,     ///< B a verrouille, A peut encore editer
		BothLocked,  ///< les 2 ont verrouille, en attente de confirmation
		Committed,   ///< echange execute (etat terminal)
		Cancelled    ///< un des deux a annule (etat terminal)
	};

	struct TradeOffer
	{
		std::vector<ItemGuid> items;
		uint64_t              copper = 0;
	};

	class TradeSession
	{
	public:
		TradeSession(PlayerId a, PlayerId b) : m_a(a), m_b(b) {}

		PlayerId    PlayerA()  const { return m_a; }
		PlayerId    PlayerB()  const { return m_b; }
		TradeState  State()    const { return m_state; }
		const TradeOffer& OfferA() const { return m_offerA; }
		const TradeOffer& OfferB() const { return m_offerB; }

		/// Edition d'offer (interdit si lock cote auteur ou etat terminal).
		bool SetOffer(PlayerId who, TradeOffer o)
		{
			if (m_state == TradeState::Committed || m_state == TradeState::Cancelled)
				return false;
			if (who == m_a)
			{
				if (m_state == TradeState::LockedA || m_state == TradeState::BothLocked)
					return false;
				m_offerA = std::move(o);
				return true;
			}
			if (who == m_b)
			{
				if (m_state == TradeState::LockedB || m_state == TradeState::BothLocked)
					return false;
				m_offerB = std::move(o);
				return true;
			}
			return false;
		}

		/// Lock cote \p who. Si les 2 sont locked passe a BothLocked.
		bool Lock(PlayerId who)
		{
			if (m_state == TradeState::Committed || m_state == TradeState::Cancelled)
				return false;
			if (who == m_a)
			{
				if (m_state == TradeState::Open)     m_state = TradeState::LockedA;
				else if (m_state == TradeState::LockedB) m_state = TradeState::BothLocked;
				else return false;
				return true;
			}
			if (who == m_b)
			{
				if (m_state == TradeState::Open)     m_state = TradeState::LockedB;
				else if (m_state == TradeState::LockedA) m_state = TradeState::BothLocked;
				else return false;
				return true;
			}
			return false;
		}

		/// Commit n'est valide qu'a partir de BothLocked.
		bool Commit()
		{
			if (m_state != TradeState::BothLocked) return false;
			m_state = TradeState::Committed;
			return true;
		}

		/// Cancel possible a tout moment non-terminal.
		bool Cancel(PlayerId who)
		{
			if (who != m_a && who != m_b) return false;
			if (m_state == TradeState::Committed || m_state == TradeState::Cancelled)
				return false;
			m_state = TradeState::Cancelled;
			return true;
		}

	private:
		PlayerId   m_a;
		PlayerId   m_b;
		TradeOffer m_offerA;
		TradeOffer m_offerB;
		TradeState m_state = TradeState::Open;
	};
}
