// Carte à découvrir, côté client. Voir MapDiscoveryClient.h.

#include "src/client/world/discovery/MapDiscoveryClient.h"

namespace engine::client
{
	namespace disc = engine::world::discovery;
	namespace cells = engine::world::cells;

	void MapDiscoveryClient::Reset()
	{
		m_cells.clear();
		m_dirty.clear();
		m_hasLastTile = false;
	}

	int MapDiscoveryClient::RevealAround(double x, double z)
	{
		const disc::TileRef here = disc::TileAt(x, z);
		if (m_hasLastTile && here.cell == m_lastTile.cell && here.tx == m_lastTile.tx && here.tz == m_lastTile.tz)
			return 0;
		m_hasLastTile = true;
		m_lastTile = here;
		int added = 0;
		disc::ForEachTileInDisc(x, z, disc::kRevealRadiusM, [&](const disc::TileRef& t) {
			if (!cells::IsInLoreGrid(t.cell))
				return;
			const uint32_t id = cells::CellZoneId(t.cell);
			if (m_cells[id].Set(t.tx, t.tz))
			{
				m_dirty.insert(id);
				++added;
			}
		});
		return added;
	}

	std::optional<uint16_t> MapDiscoveryClient::OnServerPage(const engine::network::MapDiscoveryResponsePayload& page)
	{
		if (page.error != 0u)
			return std::nullopt;
		for (const engine::network::MapDiscoveryCell& c : page.cells)
		{
			if (!cells::IsCellZoneId(c.cellZoneId) || c.bitmap.Empty())
				continue;
			disc::CellBitmap& mine = m_cells[c.cellZoneId];
			mine.Merge(c.bitmap);
			// Si le serveur a tout ce que l'on a, rien à renvoyer pour cette cellule.
			disc::CellBitmap server = c.bitmap;
			if (!server.Merge(mine))
				m_dirty.erase(c.cellZoneId);
		}
		if (page.nextIndex == 0u)
			return std::nullopt;
		return page.nextIndex;
	}

	std::vector<engine::network::MapDiscoveryCell> MapDiscoveryClient::TakeDirty(size_t maxCells)
	{
		std::vector<engine::network::MapDiscoveryCell> out;
		for (auto it = m_dirty.begin(); it != m_dirty.end() && out.size() < maxCells;)
		{
			const auto found = m_cells.find(*it);
			if (found != m_cells.end())
				out.push_back(engine::network::MapDiscoveryCell{ *it, found->second });
			it = m_dirty.erase(it);
		}
		return out;
	}

	void MapDiscoveryClient::MarkDirty(const std::vector<engine::network::MapDiscoveryCell>& cellsIn)
	{
		for (const auto& c : cellsIn)
			m_dirty.insert(c.cellZoneId);
	}

	bool MapDiscoveryClient::IsDiscovered(double x, double z) const
	{
		const disc::TileRef t = disc::TileAt(x, z);
		const disc::CellBitmap* bm = Find(t.cell);
		return bm != nullptr && bm->Get(t.tx, t.tz);
	}

	const disc::CellBitmap* MapDiscoveryClient::Find(cells::CellCoord cell) const
	{
		if (!cells::IsInLoreGrid(cell))
			return nullptr;
		const auto it = m_cells.find(cells::CellZoneId(cell));
		return it == m_cells.end() ? nullptr : &it->second;
	}
}
