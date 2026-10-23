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

	uint32_t RegionActivity::Key(cells::CellCoord cell)
	{
		return static_cast<uint32_t>(cell.n + 500) * 1000u + static_cast<uint32_t>(cell.e + 500);
	}

	void RegionActivity::AddPlayer(double x, double z)
	{
		const cells::CellCoord home = cells::CellFromPosition(x, z);
		++m_players[Key(home)];
		for (int dn = -1; dn <= 1; ++dn)
		{
			for (int de = -1; de <= 1; ++de)
			{
				if (dn == 0 && de == 0) continue;
				const cells::CellCoord neighbour{ home.n + dn, home.e + de };
				if (cells::DistanceToCell(neighbour, x, z) <= m_marginMeters)
				{
					m_players.try_emplace(Key(neighbour), 0u);
				}
			}
		}
	}

	bool RegionActivity::IsActive(cells::CellCoord cell) const
	{
		return m_players.find(Key(cell)) != m_players.end();
	}

	bool RegionActivity::IsActiveAt(double x, double z) const
	{
		return IsActive(cells::CellFromPosition(x, z));
	}

	bool RegionActivity::IsOccupied(cells::CellCoord cell) const
	{
		const auto it = m_players.find(Key(cell));
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
}
