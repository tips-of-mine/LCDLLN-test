#pragma once
// CMANGOS.03 (Phase 2.03a) — GridState : machine à états par cellule
// pour permettre un tick conditionnel (ne ticker que les cellules
// "Active") et une libération automatique des cellules inutilisées.
//
// Conçue comme un *tracker* à côté de CellGrid (cf. SpatialPartition.h).
// L'API existante de CellGrid n'est PAS modifiée : ce module est
// purement additif, et le shard appelle `OnPlayerEnter / OnPlayerLeave`
// + `Tick(now)` à ses moments propres.
//
// 4 états :
//   - Loaded   : cellule connue, pas (encore) de joueur dedans
//   - Active   : ≥ 1 joueur dans la cellule → ticker
//   - Idle     : 0 joueur depuis `idleTimeout` → ne pas ticker mais
//                garder en RAM (creatures peuvent encore exister)
//   - Removal  : 0 joueur depuis `unloadTimeout` → prêt à décharger
//
// Transitions :
//   Loaded  -> Active        : OnPlayerEnter
//   Active  -> Loaded        : OnPlayerLeave (si playerCount tombe à 0)
//   Loaded  -> Idle          : Tick après idleTimeout sans joueur
//   Idle    -> Active        : OnPlayerEnter
//   Idle    -> Removal       : Tick après unloadTimeout cumulé
//
// IMPORTANT : tous les timers utilisent `std::chrono::steady_clock`
// (jamais wall_clock) — un changement d'heure système (NTP, suspend)
// ne doit PAS décharger toute la map.

#include "engine/server/SpatialPartition.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	enum class GridState : uint8_t
	{
		Loaded  = 0,
		Active  = 1,
		Idle    = 2,
		Removal = 3,
	};

	const char* GridStateLabel(GridState s) noexcept;

	struct GridStateConfig
	{
		/// Délai sans joueur avant `Active|Loaded → Idle`.
		std::chrono::seconds idleTimeout{60};
		/// Délai sans joueur (cumulé depuis `Loaded`/`Idle`) avant
		/// `→ Removal`. Doit être ≥ idleTimeout.
		std::chrono::seconds unloadTimeout{300};
	};

	/// Tracker par cellule, en mémoire RAM. Pas thread-safe : à appeler
	/// depuis le thread de tick du shard.
	class GridStateTracker final
	{
	public:
		using Clock     = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		explicit GridStateTracker(GridStateConfig cfg = {}) : m_cfg(cfg) {}

		/// Référence pour ajustement après création (utile en tests).
		GridStateConfig& MutableConfig() { return m_cfg; }
		const GridStateConfig& Config() const { return m_cfg; }

		/// Notifie qu'un joueur entre dans \p cell. Crée l'entry si
		/// inexistante. Transition Loaded/Idle → Active.
		void OnPlayerEnter(const CellCoord& cell, TimePoint now);

		/// Notifie qu'un joueur quitte \p cell. Si le compteur tombe à
		/// 0, démarre le timer "no-player". Pas de transition immédiate
		/// vers Idle (ça arrive au Tick suivant après `idleTimeout`).
		void OnPlayerLeave(const CellCoord& cell, TimePoint now);

		/// Avance la state machine en fonction du temps écoulé. Doit
		/// être appelé périodiquement (tick shard, ou cron à 1Hz).
		void Tick(TimePoint now);

		/// État courant d'une cellule. `Loaded` si inexistante.
		GridState StateOf(const CellCoord& cell) const;

		/// Compteur joueurs courant (0 si inexistante).
		int PlayerCount(const CellCoord& cell) const;

		/// Liste des cellules dans l'état \p s (snapshot, ordre non
		/// spécifié). Utile pour le tick conditionnel : "donne-moi
		/// toutes les cellules Active".
		std::vector<CellCoord> CellsInState(GridState s) const;

		/// Retire toutes les cellules dans l'état Removal et retourne
		/// la liste retirée. Caller doit ensuite décharger ses propres
		/// structures pour ces cellules (entités, creatures...).
		std::vector<CellCoord> DrainRemovalCells();

		/// Nombre total de cellules trackées.
		size_t Size() const { return m_cells.size(); }

		/// Reset complet — utile en tests.
		void Clear();

	private:
		struct Entry
		{
			GridState   state         = GridState::Loaded;
			int         playerCount   = 0;
			/// Timestamp du dernier "no-player" (0 → inactif). Sert
			/// pour les transitions vers Idle puis Removal.
			TimePoint   lastEmptySince{};
		};

		GridStateConfig                                              m_cfg;
		std::unordered_map<CellCoord, Entry, CellCoordHash>          m_cells;
	};
}
