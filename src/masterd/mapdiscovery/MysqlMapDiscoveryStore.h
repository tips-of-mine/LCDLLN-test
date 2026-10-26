#pragma once
// MysqlMapDiscoveryStore — persistance de la carte à découvrir par personnage
// (table character_map_discovery, migration 0076). Une ligne par cellule
// visitée : masque brut de 800 octets (cf. src/shared/world/MapDiscovery.h).
// Fusion OU : une case découverte ne redevient jamais cachée.
// ETAPE2.5 — lecture seule côté master : la carte est écrite par le shard
// propriétaire (ServerApp::WriteCharacterStateInTx).

#include "src/shared/world/MapDiscovery.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server
{
	class MysqlMapDiscoveryStore
	{
	public:
		/// Masque d'une cellule tel que stocké.
		struct StoredCell
		{
			uint32_t cellZoneId = 0;
			engine::world::discovery::CellBitmap bitmap;
		};

		explicit MysqlMapDiscoveryStore(engine::server::db::ConnectionPool* pool) : m_pool(pool) {}

		/// true si le pool DB est initialisé (mode no-DB : tout no-op).
		bool IsAvailable() const;

		/// Page de cellules du personnage, triées par cell_zone_id.
		/// \param startIndex rang de la première ligne. \param maxCount taille de page.
		/// \param outTotal nombre total de cellules du personnage.
		/// \return false en erreur DB.
		bool ListPage(uint64_t characterId, uint32_t startIndex, uint32_t maxCount,
			std::vector<StoredCell>& outCells, uint32_t& outTotal);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
