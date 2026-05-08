#pragma once
// CMANGOS.16 (Phase 1b) — GraveyardManager : table de points de respawn
// par map + filtre faction + closest.

#include <cstdint>
#include <optional>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Faction id : 0 = neutral, 1+ = faction spécifique.
	using FactionId = uint8_t;

	/// Définition d'un graveyard.
	struct Graveyard
	{
		uint32_t id        = 0;
		uint32_t mapId     = 0;
		float positionX    = 0.0f;
		float positionY    = 0.0f;
		float positionZ    = 0.0f;
		FactionId faction  = 0;     ///< 0 = neutral, accepte toutes factions
		uint32_t zoneId    = 0;
	};

	class GraveyardManager
	{
	public:
		GraveyardManager() = default;
		~GraveyardManager() = default;
		GraveyardManager(const GraveyardManager&) = delete;
		GraveyardManager& operator=(const GraveyardManager&) = delete;

		/// Charge `graveyards` depuis la DB. \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool);

		/// Cherche le graveyard valide le plus proche pour la position et faction
		/// données. Un graveyard est valide si `faction == 0` (neutral) OU
		/// `faction == requestedFaction`. Retourne nullopt si aucun candidat
		/// sur cette map.
		std::optional<Graveyard> ClosestGraveyard(uint32_t mapId,
			float posX, float posY, float posZ, FactionId requestedFaction) const;

		size_t Size() const { return m_graveyards.size(); }

	private:
		std::vector<Graveyard> m_graveyards;  // pas de map — N petit, scan linéaire OK
		bool m_loaded = false;
	};
}
