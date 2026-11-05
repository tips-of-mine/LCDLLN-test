#include "src/shardd/world/RegionActivity.h"

#include <algorithm>

namespace engine::server
{
	namespace cells = engine::world::cells;

	RegionActivity::RegionActivity(double marginMeters)
	{
		SetMarginMeters(marginMeters);
	}

	void RegionActivity::SetMarginMeters(double marginMeters)
	{
		// NaN → marge par défaut ; sinon bornée à une demi-cellule.
		if (!(marginMeters == marginMeters))
		{
			marginMeters = kDefaultMarginMeters;
		}
		m_marginMeters = std::clamp(marginMeters, 0.0, cells::kCellHalfMeters);
	}

	void RegionActivity::Begin()
	{
		m_players.clear();
	}

	uint32_t RegionActivity::Key(const cells::MapCell& region)
	{
		return cells::MapCellZoneId(region);
	}

	void RegionActivity::AddPlayer(double x, double z, cells::CellMap map)
	{
		const cells::CellCoord home = cells::CellFromPosition(x, z);
		// Position hors des clés représentables (joueur hors de toute grille) : ignorée.
		if (home.n < -500 || home.n >= 500 || home.e < -500 || home.e >= 500)
			return;
		++m_players[Key({ home, map })];
		for (int dn = -1; dn <= 1; ++dn)
		{
			for (int de = -1; de <= 1; ++de)
			{
				if (dn == 0 && de == 0) continue;
				const cells::CellCoord neighbour{ home.n + dn, home.e + de };
				// ETAPE4.3 — bord de la carte : une voisine hors [−500, 500[ ne serait pas
				// représentable dans la clé (ActiveCells la décoderait de travers).
				if (neighbour.n < -500 || neighbour.n >= 500 || neighbour.e < -500 || neighbour.e >= 500)
					continue;
				if (cells::DistanceToCell(neighbour, x, z) <= m_marginMeters)
				{
					m_players.try_emplace(Key({ neighbour, map }), 0u);
				}
			}
		}
	}

	bool RegionActivity::IsActive(const cells::MapCell& region) const
	{
		return m_players.find(Key(region)) != m_players.end();
	}

	bool RegionActivity::IsActiveAt(double x, double z, cells::CellMap map) const
	{
		return IsActive({ cells::CellFromPosition(x, z), map });
	}

	bool RegionActivity::IsOccupied(const cells::MapCell& region) const
	{
		const auto it = m_players.find(Key(region));
		return it != m_players.end() && it->second > 0u;
	}

	size_t RegionActivity::OccupiedCount() const
	{
		size_t count = 0;
		for (const auto& [key, players] : m_players)
		{
			(void)key;
			if (players > 0u) ++count;
		}
		return count;
	}

	std::vector<cells::MapCell> RegionActivity::ActiveCells() const
	{
		std::vector<cells::MapCell> out;
		out.reserve(m_players.size());
		for (const auto& [key, count] : m_players)
		{
			(void)count;
			if (const std::optional<cells::MapCell> region = cells::MapCellFromZoneId(key))
				out.push_back(*region);
		}
		return out;
	}
}
