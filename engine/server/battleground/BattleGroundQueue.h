#pragma once
// CMANGOS.10 (Phase 4.10a) — BattleGroundQueue : matchmaking de file
// d'attente PvP instancie (BG type, taille equipe, faction-balanced).
// Header-only.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::server::battleground
{
	using BgType    = uint16_t;
	using PlayerId  = uint64_t;
	using FactionId = uint8_t;  ///< 0 = Alliance, 1 = Horde

	struct QueueEntry
	{
		PlayerId  player = 0;
		FactionId fac    = 0;
		uint64_t  enqueueMs = 0;
	};

	struct MatchProposal
	{
		BgType                     bg;
		std::vector<PlayerId>      alliance;
		std::vector<PlayerId>      horde;
	};

	/// Queue par bg type. Politique : equipes equilibrees par faction,
	/// FIFO sur l'ordre d'arrivee.
	class BattleGroundQueue
	{
	public:
		void Enqueue(BgType bg, PlayerId p, FactionId fac, uint64_t nowMs)
		{
			m_queues[bg].push_back({p, fac, nowMs});
		}

		size_t Size(BgType bg) const
		{
			auto it = m_queues.find(bg);
			return (it == m_queues.end()) ? 0 : it->second.size();
		}

		/// Tente de former un match avec \p teamSize joueurs par cote.
		/// Retourne nullopt si pas assez de candidats par faction.
		/// Retire les joueurs selectionnes de la queue.
		std::optional<MatchProposal> TryMakeMatch(BgType bg, size_t teamSize)
		{
			auto it = m_queues.find(bg);
			if (it == m_queues.end()) return std::nullopt;

			std::vector<size_t> aIdx, hIdx;
			auto& q = it->second;
			for (size_t i = 0; i < q.size(); ++i)
			{
				if (q[i].fac == 0 && aIdx.size() < teamSize) aIdx.push_back(i);
				else if (q[i].fac == 1 && hIdx.size() < teamSize) hIdx.push_back(i);
				if (aIdx.size() == teamSize && hIdx.size() == teamSize) break;
			}
			if (aIdx.size() != teamSize || hIdx.size() != teamSize) return std::nullopt;

			MatchProposal p;
			p.bg = bg;
			for (auto i : aIdx) p.alliance.push_back(q[i].player);
			for (auto i : hIdx) p.horde.push_back(q[i].player);

			// Retire en ordre decroissant pour preserver les indices.
			std::vector<size_t> all;
			all.insert(all.end(), aIdx.begin(), aIdx.end());
			all.insert(all.end(), hIdx.begin(), hIdx.end());
			std::sort(all.begin(), all.end(), std::greater<size_t>());
			for (auto i : all) q.erase(q.begin() + i);

			return p;
		}

	private:
		std::unordered_map<BgType, std::vector<QueueEntry>> m_queues;
	};
}
