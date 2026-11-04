#pragma once

// Monde « Royaumes » — activation des RÉGIONS (cellules de 10 km).
//
// Le monde est une seule carte découpée en cellules de 10 km × 10 km. Une
// cellule sans joueur ne doit rien coûter : ses spawners ne sont même pas
// évalués et aucun mob n'y est créé. Une cellule est ACTIVE si un joueur s'y
// trouve, ou si un joueur se tient à moins de `marginMeters` de son bord (le
// joueur voit et attire déjà ce qui est de l'autre côté de la frontière).
//
// Seconde carte (Les Enfers) : une région est une cellule D'UNE carte
// (cells::MapCell) — les cellules (0,0) des Royaumes et des Enfers sont distinctes.
//
// Usage (shard, une fois par seconde) :
//   activity.Begin();
//   for (joueur) activity.AddPlayer(x, z, carte);
//   … activity.IsActive(cellule) …
//
// Module PUR (WorldCells uniquement) → testé en CI (region_activity_tests).

#include "src/shared/world/WorldCells.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Ensemble des cellules actives, recalculé à partir des positions des joueurs.
	class RegionActivity final
	{
	public:
		/// Marge par défaut (m) : doit dépasser le rayon d'intérêt (7×7 × 100 m = ±350 m).
		static constexpr double kDefaultMarginMeters = 1000.0;

		/// \param marginMeters distance (m) au bord d'une cellule voisine en deçà de
		///        laquelle cette voisine est activée. Bornée à [0, 5000].
		explicit RegionActivity(double marginMeters = kDefaultMarginMeters);

		/// Change la marge (m). Bornée à [0, 5000]. Prise en compte au prochain AddPlayer.
		void SetMarginMeters(double marginMeters);
		/// Marge courante (m), après bornage.
		double MarginMeters() const { return m_marginMeters; }

		/// Oublie toutes les activations (début d'un recalcul).
		void Begin();

		/// Active la cellule du joueur (position GLOBALE en m sur la carte \p map) et
		/// les voisines (8 au plus, même carte) dont le bord est à moins de la marge.
		void AddPlayer(double x, double z, engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes);

		/// Vrai si la région (cellule d'une carte ; une CellCoord seule = Royaumes) est active.
		bool IsActive(const engine::world::cells::MapCell& region) const;
		/// Vrai si la cellule du point global (x, z) de la carte \p map est active.
		bool IsActiveAt(double x, double z, engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes) const;
		/// Vrai si au moins un joueur se trouve DANS la région.
		bool IsOccupied(const engine::world::cells::MapCell& region) const;

		/// Nombre de cellules actives / occupées (télémétrie, logs).
		size_t ActiveCount() const { return m_players.size(); }
		size_t OccupiedCount() const;
		/// ETAPE4.3 — liste des régions actives (ordre non spécifié) : entrée de
		/// RegionLifecycle::Update.
		std::vector<engine::world::cells::MapCell> ActiveCells() const;

	private:
		/// Clé compacte d'une région = son identifiant de zone (unique toutes cartes ;
		/// n, e ∈ [−500, 500[ : toujours vrai pour un joueur, le mur invisible le garde
		/// dans les cellules générées).
		static uint32_t Key(const engine::world::cells::MapCell& region);

		double m_marginMeters = kDefaultMarginMeters;
		/// Cellule active → nombre de joueurs DEDANS (0 = activée par la marge seulement).
		std::unordered_map<uint32_t, uint32_t> m_players;
	};
}
