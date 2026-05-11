#pragma once
// Wave 21 — DungeonMap : map instance, locked par player ou group. Chaque
// instance accepte un set de player IDs au moment de la creation (le
// "owner set"). Apres scellement, AddPlayer rejette tout id hors du set.
//
// Pattern WoW-like : un raid de 10 personnes entre dans un dungeon, le
// dungeon se ferme aux autres joueurs. Wipe + reset hors du dungeon =
// nouvelle instance (allouee separement par le caller).
//
// Capacite max optionnelle : si maxCapacity > 0, AddPlayer rejette aussi
// quand la limite est atteinte (defense supplementaire contre l'over-fill).

#include "src/shardd/maps/Map.h"

#include <cstdint>

namespace engine::server::maps
{
	class DungeonMap final : public Map
	{
	public:
		/// \param mapId       id template
		/// \param instanceId  instance unique
		/// \param ownerSet    EntityIds des players autorises (set Wave 21 "lock list")
		/// \param maxCapacity 0 = pas de limite stricte ; > 0 = cap a N players
		DungeonMap(MapId mapId, InstanceId instanceId,
			std::unordered_set<uint64_t> ownerSet,
			uint32_t maxCapacity = 0)
			: Map(mapId, instanceId)
			, m_ownerSet(std::move(ownerSet))
			, m_maxCapacity(maxCapacity)
		{}

		MapType Type() const noexcept override { return MapType::Dungeon; }

		/// AddPlayer reussi UNIQUEMENT si :
		/// 1. \p playerId est dans le ownerSet du dungeon, ET
		/// 2. la capacite max n'est pas atteinte (si maxCapacity > 0).
		/// Sinon, retourne false sans rien modifier.
		bool AddPlayer(uint64_t playerId) override
		{
			if (m_ownerSet.count(playerId) == 0)
				return false;
			if (m_maxCapacity > 0 && m_players.size() >= m_maxCapacity
				&& m_players.count(playerId) == 0)
				return false;
			m_players.insert(playerId);
			return true;
		}

		const std::unordered_set<uint64_t>& OwnerSet() const noexcept { return m_ownerSet; }
		uint32_t MaxCapacity() const noexcept { return m_maxCapacity; }

	private:
		std::unordered_set<uint64_t> m_ownerSet;
		uint32_t                     m_maxCapacity;
	};
}
