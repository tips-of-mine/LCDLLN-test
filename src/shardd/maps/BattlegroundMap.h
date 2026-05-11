#pragma once
// Wave 21 — BattlegroundMap : map courte duree avec scoreboard integre.
// Pas de lock par owner set (vs Dungeon) : tous les players matchmakees par
// LfgQueue / BG queue peuvent entrer. Capacite stricte (souvent 10 ou 15
// par camp).
//
// Scoreboard : map score par player ID (kills, captures, points, etc.).
// Wave 21 livre la struct minimaliste — l'integration avec un systeme de
// scoring riche viendra dans une Wave dediee (CMANGOS.10 BattleGround
// step 2).

#include "src/shardd/maps/Map.h"

#include <cstdint>
#include <unordered_map>

namespace engine::server::maps
{
	/// Score d'un player sur un BG. Champs basiques : kills + points.
	struct BattlegroundScore
	{
		uint32_t kills      = 0;
		uint32_t deaths     = 0;
		uint32_t honorPoints = 0;  ///< unite generique (capture/objectif/etc.)
	};

	class BattlegroundMap final : public Map
	{
	public:
		/// \param mapId       id template
		/// \param instanceId  instance unique
		/// \param maxCapacity capacite max stricte (ex: 30 pour 15v15)
		BattlegroundMap(MapId mapId, InstanceId instanceId, uint32_t maxCapacity)
			: Map(mapId, instanceId)
			, m_maxCapacity(maxCapacity)
		{}

		MapType Type() const noexcept override { return MapType::Battleground; }

		/// AddPlayer reussi si la capacite max n'est pas atteinte. Idempotent
		/// si le player est deja present (ne consomme pas un slot a nouveau).
		bool AddPlayer(uint64_t playerId) override
		{
			if (m_players.count(playerId) > 0)
				return true;  // idempotent
			if (m_players.size() >= m_maxCapacity)
				return false;
			m_players.insert(playerId);
			// Initialise un score a 0 pour ce player.
			m_scores[playerId] = BattlegroundScore{};
			return true;
		}

		void RemovePlayer(uint64_t playerId) override
		{
			Map::RemovePlayer(playerId);
			// Garde le score (utile pour le scoreboard post-match meme si le
			// player quitte avant la fin).
		}

		/// Increment le compteur \p kind sur le score du player. No-op si
		/// le player n'a jamais ete dans ce BG (pas de score initialise).
		void AddKill(uint64_t playerId)
		{
			auto it = m_scores.find(playerId);
			if (it != m_scores.end()) it->second.kills++;
		}

		void AddDeath(uint64_t playerId)
		{
			auto it = m_scores.find(playerId);
			if (it != m_scores.end()) it->second.deaths++;
		}

		void AddHonor(uint64_t playerId, uint32_t amount)
		{
			auto it = m_scores.find(playerId);
			if (it != m_scores.end()) it->second.honorPoints += amount;
		}

		/// Score d'un player. Retourne {0,0,0} si player jamais initialise.
		BattlegroundScore ScoreOf(uint64_t playerId) const
		{
			auto it = m_scores.find(playerId);
			return (it != m_scores.end()) ? it->second : BattlegroundScore{};
		}

		const std::unordered_map<uint64_t, BattlegroundScore>& Scores() const noexcept
		{
			return m_scores;
		}

		uint32_t MaxCapacity() const noexcept { return m_maxCapacity; }

	private:
		uint32_t                                          m_maxCapacity;
		std::unordered_map<uint64_t, BattlegroundScore>   m_scores;
	};
}
