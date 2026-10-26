#pragma once
// ETAPE2.5 — MapDiscoveryTracker : carte à découvrir calculée PAR LE SHARD.
//
// Le shard connaît la position autoritaire du joueur : il révèle lui-même les
// cases de 125 m autour de lui (mêmes règles que le client, cf.
// src/shared/world/MapDiscovery.h) et les écrit en base dans la transaction de
// sauvegarde du personnage (fusion OU dans character_map_discovery). Le master
// ne fait plus que relire la carte pour le client (opcodes 208/209).
//
// Le traceur ne garde que les cases révélées PENDANT la session : la fusion OU
// en base conserve celles des sessions précédentes.
//
// Pur (aucune base, aucun réseau) : testé par map_discovery_tracker_tests.

#include "src/shared/world/MapDiscovery.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::server
{
	class MapDiscoveryTracker
	{
	public:
		/// Révèle les cases autour du point GLOBAL (x, z) (m). Ne recalcule le
		/// disque que si le joueur a changé de case. \return cases nouvellement révélées.
		int RevealAround(double x, double z);

		/// Cellules touchées pendant la session (identifiant CellZoneId + masque des
		/// cases révélées pendant la session), triées par identifiant.
		std::vector<std::pair<uint32_t, engine::world::discovery::CellBitmap>> SessionCells() const;

		/// Nombre de cellules touchées pendant la session.
		size_t CellCount() const { return m_cells.size(); }

		/// Oublie tout (nouvelle session).
		void Reset();

	private:
		std::unordered_map<uint32_t, engine::world::discovery::CellBitmap> m_cells;
		bool m_hasLastTile = false;
		engine::world::discovery::TileRef m_lastTile{};
	};
}
