#pragma once
// ETAPE4.3 (chantiers 2.1 et 2.4 du rapport City of Heroes) — cycle de vie des régions
// (cellules de 10 km du monde « Royaumes ») côté shard.
//
// RegionActivity dit, chaque seconde, quelles régions sont ACTIVES (un joueur dedans ou
// à moins de la marge de leur bord). Ce module ajoute la mémoire du temps, avec le
// GridStateTracker existant (états Loaded / Active / Idle / Removal) :
//   - Active  : la région est active (au moins un joueur la « tient ») ;
//   - Loaded  : plus active depuis moins de idleTimeout (60 s) : ses spawners sont
//               encore évalués (retraits en cours, retour rapide d'un joueur) ;
//   - Idle    : plus active depuis idleTimeout : FROIDE, ses spawners ne sont plus du
//               tout parcourus (coût nul) ;
//   - Removal : plus active depuis unloadTimeout (300 s) : la région est déchargée
//               (DrainUnloaded) et oubliée ; le shard remet à neuf ses spawners
//               (délais de réapparition), comme si personne n'y était jamais venu.
// Une région jamais visitée n'est pas suivie : elle est froide d'office.
//
// Module PUR (pas de réseau ni de ServerApp) : testé seul (region_lifecycle_tests).
// Pas thread-safe : thread de tick du shard.

#include "src/shardd/world/GridState.h"
#include "src/shared/world/WorldCells.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	class RegionLifecycle final
	{
	public:
		using Clock = GridStateTracker::Clock;
		using TimePoint = GridStateTracker::TimePoint;

		explicit RegionLifecycle(GridStateConfig config = {});

		/// Change les délais (Idle, déchargement). unloadTimeout est relevé à
		/// idleTimeout s'il est plus court.
		void Configure(GridStateConfig config);
		const GridStateConfig& Config() const { return m_tracker.Config(); }

		/// Nouvel ensemble de régions actives (calculé par RegionActivity) à l'instant
		/// \p now : entrées / sorties notifiées au tracker, puis avancement des délais.
		/// À appeler à chaque recalcul (1 Hz).
		/// \return les régions qui viennent de devenir FROIDES (Idle) à cet appel.
		std::vector<engine::world::cells::CellCoord> Update(
			const std::vector<engine::world::cells::CellCoord>& activeRegions, TimePoint now);

		/// Vrai si les spawners de la région doivent être évalués : région suivie et
		/// Active ou Loaded (pas encore froide).
		bool IsWarm(engine::world::cells::CellCoord region) const;

		/// État de la région (Loaded si jamais suivie ou déjà déchargée).
		GridState StateOf(engine::world::cells::CellCoord region) const;

		/// Régions arrivées au déchargement (Removal) depuis le dernier appel ; elles
		/// sont oubliées (une visite ultérieure repart de zéro).
		std::vector<engine::world::cells::CellCoord> DrainUnloaded();

		/// Régions suivies (télémétrie) et nombre de régions chaudes (Active + Loaded).
		size_t TrackedCount() const { return m_known.size(); }
		size_t WarmCount() const;

	private:
		static uint32_t Key(engine::world::cells::CellCoord region);
		static CellCoord ToGrid(engine::world::cells::CellCoord region);
		static engine::world::cells::CellCoord FromGrid(CellCoord cell);

		GridStateTracker m_tracker;
		std::unordered_set<uint32_t> m_known;   ///< régions suivies (visitées, pas encore déchargées)
		std::unordered_set<uint32_t> m_active;  ///< régions actives au dernier Update
		std::unordered_set<uint32_t> m_idle;    ///< régions déjà signalées froides
	};
}
