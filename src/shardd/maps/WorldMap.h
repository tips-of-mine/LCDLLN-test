#pragma once
// Wave 21 — WorldMap : map ouverte, persistante. 1 seule instance globale
// par mapId (par opposition aux Dungeon/Battleground qui peuvent avoir N
// instances en parallele). Tous les players du shard sur ce map se
// retrouvent dans le meme WorldMap.
//
// Pas de lock d'acces : AddPlayer retourne toujours true. La capacite est
// limitee par la grille spatiale (Wave 18 SpatialPartition + GridVisitor)
// et le tick scheduler du shard, pas par WorldMap lui-meme.

#include "src/shardd/maps/Map.h"

#include <cstdint>

namespace engine::server::maps
{
	class WorldMap final : public Map
	{
	public:
		WorldMap(MapId mapId, InstanceId instanceId)
			: Map(mapId, instanceId)
		{}

		MapType Type() const noexcept override { return MapType::World; }

		/// WorldMap accepte tous les players (pas de lock). Retourne true
		/// systematiquement (idempotent : si deja present, no-op silent).
		bool AddPlayer(uint64_t playerId) override
		{
			m_players.insert(playerId);
			return true;
		}
	};
}
