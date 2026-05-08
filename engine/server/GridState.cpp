#include "engine/server/GridState.h"

namespace engine::server
{
	const char* GridStateLabel(GridState s) noexcept
	{
		switch (s)
		{
			case GridState::Loaded:  return "Loaded";
			case GridState::Active:  return "Active";
			case GridState::Idle:    return "Idle";
			case GridState::Removal: return "Removal";
		}
		return "?";
	}

	void GridStateTracker::OnPlayerEnter(const CellCoord& cell, TimePoint now)
	{
		auto& e = m_cells[cell];
		++e.playerCount;
		// Tout entry avec ≥ 1 joueur passe à Active. Reset le timer
		// "no-player" — on n'est plus en attente.
		e.state = GridState::Active;
		e.lastEmptySince = TimePoint{};
		(void)now;
	}

	void GridStateTracker::OnPlayerLeave(const CellCoord& cell, TimePoint now)
	{
		auto it = m_cells.find(cell);
		if (it == m_cells.end())
			return;
		auto& e = it->second;
		if (e.playerCount > 0)
			--e.playerCount;
		if (e.playerCount == 0)
		{
			// Démarrer le timer no-player (ou prolonger si déjà actif).
			// On NE retombe PAS immédiatement à Loaded depuis Active —
			// c'est le Tick qui décide. Mais on remet à Loaded pour que
			// le ticker shard ne tape plus dessus inutilement entre
			// deux Tick.
			if (e.state == GridState::Active)
				e.state = GridState::Loaded;
			if (e.lastEmptySince == TimePoint{})
				e.lastEmptySince = now;
		}
	}

	void GridStateTracker::Tick(TimePoint now)
	{
		for (auto& [coord, e] : m_cells)
		{
			if (e.playerCount > 0)
				continue;  // Active → reste Active
			if (e.lastEmptySince == TimePoint{})
			{
				// Cellule jamais visitée OU revenue d'Active sans
				// passer par OnPlayerLeave (cas anormal). Démarrer le
				// timer maintenant.
				e.lastEmptySince = now;
				continue;
			}
			const auto elapsed = now - e.lastEmptySince;
			if (elapsed >= m_cfg.unloadTimeout)
				e.state = GridState::Removal;
			else if (elapsed >= m_cfg.idleTimeout)
			{
				if (e.state == GridState::Loaded)
					e.state = GridState::Idle;
				// Idle reste Idle jusqu'à Removal.
			}
		}
	}

	GridState GridStateTracker::StateOf(const CellCoord& cell) const
	{
		auto it = m_cells.find(cell);
		return (it == m_cells.end()) ? GridState::Loaded : it->second.state;
	}

	int GridStateTracker::PlayerCount(const CellCoord& cell) const
	{
		auto it = m_cells.find(cell);
		return (it == m_cells.end()) ? 0 : it->second.playerCount;
	}

	std::vector<CellCoord> GridStateTracker::CellsInState(GridState s) const
	{
		std::vector<CellCoord> out;
		out.reserve(m_cells.size());
		for (const auto& [coord, e] : m_cells)
		{
			if (e.state == s)
				out.push_back(coord);
		}
		return out;
	}

	std::vector<CellCoord> GridStateTracker::DrainRemovalCells()
	{
		std::vector<CellCoord> out;
		for (auto it = m_cells.begin(); it != m_cells.end(); )
		{
			if (it->second.state == GridState::Removal)
			{
				out.push_back(it->first);
				it = m_cells.erase(it);
			}
			else
			{
				++it;
			}
		}
		return out;
	}

	void GridStateTracker::Clear()
	{
		m_cells.clear();
	}
}
