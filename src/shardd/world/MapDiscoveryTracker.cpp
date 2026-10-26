// ETAPE2.5 — MapDiscoveryTracker : voir MapDiscoveryTracker.h.

#include "src/shardd/world/MapDiscoveryTracker.h"

#include <algorithm>

namespace engine::server
{
	namespace disc = engine::world::discovery;
	namespace cells = engine::world::cells;

	int MapDiscoveryTracker::RevealAround(double x, double z)
	{
		const disc::TileRef here = disc::TileAt(x, z);
		if (m_hasLastTile && here.cell == m_lastTile.cell && here.tx == m_lastTile.tx && here.tz == m_lastTile.tz)
		{
			return 0;
		}
		m_hasLastTile = true;
		m_lastTile = here;
		int added = 0;
		disc::ForEachTileInDisc(x, z, disc::kRevealRadiusM, [&](const disc::TileRef& t) {
			if (!cells::IsInLoreGrid(t.cell))
			{
				return; // hors de la carte du lore : rien à persister
			}
			if (m_cells[cells::CellZoneId(t.cell)].Set(t.tx, t.tz))
			{
				++added;
			}
		});
		return added;
	}

	std::vector<std::pair<uint32_t, disc::CellBitmap>> MapDiscoveryTracker::SessionCells() const
	{
		std::vector<std::pair<uint32_t, disc::CellBitmap>> out(m_cells.begin(), m_cells.end());
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return out;
	}

	void MapDiscoveryTracker::Reset()
	{
		m_cells.clear();
		m_hasLastTile = false;
		m_lastTile = {};
	}
}
