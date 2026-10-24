#pragma once

// Monde « Royaumes » — carte à découvrir, côté client.
//
// Révèle les cases de 125 m autour du joueur (rayon kRevealRadiusM) au fil du
// déplacement, mémorise les cellules modifiées depuis le dernier envoi et
// fusionne les masques reçus du master (persistance par personnage, opcodes
// 208–210). La carte du monde n'affiche que les cases découvertes.
//
// Pur (aucun réseau ni rendu) : l'Engine envoie les requêtes et transmet les
// réponses. Main thread.

#include "src/shared/network/MapDiscoveryPayloads.h"
#include "src/shared/world/MapDiscovery.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::client
{
	class MapDiscoveryClient
	{
	public:
		/// Oublie tout (changement de personnage).
		void Reset();

		/// Révèle les cases autour du point GLOBAL (x, z). Ne recalcule le disque
		/// que si le joueur a changé de case. \return nombre de cases nouvellement révélées.
		int RevealAround(double x, double z);

		/// Fusionne une page reçue du master. \return startIndex de la page suivante
		/// à demander, ou nullopt s'il n'y en a plus (ou en cas d'erreur).
		std::optional<uint16_t> OnServerPage(const engine::network::MapDiscoveryResponsePayload& page);

		/// Extrait (et retire de la liste des modifiées) au plus \p maxCells cellules
		/// à envoyer au master. Vide s'il n'y a rien de nouveau.
		std::vector<engine::network::MapDiscoveryCell> TakeDirty(size_t maxCells);
		/// Remet des cellules dans la liste des modifiées (envoi échoué).
		void MarkDirty(const std::vector<engine::network::MapDiscoveryCell>& cells);
		bool HasDirty() const { return !m_dirty.empty(); }

		/// Vrai si la case contenant le point GLOBAL (x, z) est découverte.
		bool IsDiscovered(double x, double z) const;
		/// Masque d'une cellule (nullptr si aucune case découverte).
		const engine::world::discovery::CellBitmap* Find(engine::world::cells::CellCoord cell) const;
		/// Nombre de cellules ayant au moins une case découverte.
		size_t CellCount() const { return m_cells.size(); }

	private:
		std::unordered_map<uint32_t, engine::world::discovery::CellBitmap> m_cells;
		std::unordered_set<uint32_t> m_dirty;
		/// Dernière case où le disque a été calculé (évite le recalcul à chaque frame).
		bool m_hasLastTile = false;
		engine::world::discovery::TileRef m_lastTile{};
	};
}
