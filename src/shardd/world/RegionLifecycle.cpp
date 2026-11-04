// ETAPE4.3 — cycle de vie des régions côté shard. Voir RegionLifecycle.h.

#include "src/shardd/world/RegionLifecycle.h"

namespace engine::server
{
	namespace cells = engine::world::cells;

	RegionLifecycle::RegionLifecycle(GridStateConfig config)
	{
		Configure(config);
	}

	void RegionLifecycle::Configure(GridStateConfig config)
	{
		if (config.idleTimeout.count() < 0)
			config.idleTimeout = std::chrono::seconds{ 0 };
		if (config.unloadTimeout < config.idleTimeout)
			config.unloadTimeout = config.idleTimeout;
		m_tracker.MutableConfig() = config;
	}

	namespace
	{
		/// Décalage en X de la grille du tracker entre deux cartes : e ∈ [−500, 500[
		/// pour chaque carte, la carte k occupe x ∈ [k·2000 − 500, k·2000 + 500[ (int16).
		constexpr int kMapGridStride = 2000;
	}

	uint32_t RegionLifecycle::Key(const cells::MapCell& region)
	{
		// Identifiant de zone de la cellule : unique toutes cartes confondues.
		return cells::MapCellZoneId(region);
	}

	CellCoord RegionLifecycle::ToGrid(const cells::MapCell& region)
	{
		CellCoord c{};
		c.x = static_cast<int16_t>(region.cell.e + static_cast<int>(region.map) * kMapGridStride);
		c.z = static_cast<int16_t>(region.cell.n);
		return c;
	}

	cells::MapCell RegionLifecycle::FromGrid(CellCoord cell)
	{
		// x ∈ [k·2000 − 500, k·2000 + 500[ → carte k (arrondi au multiple le plus proche).
		const int x = cell.x;
		const int mapIndex = (x + kMapGridStride / 2) / kMapGridStride;
		cells::MapCell r{};
		r.map = static_cast<cells::CellMap>(mapIndex);
		r.cell.n = cell.z;
		r.cell.e = x - mapIndex * kMapGridStride;
		return r;
	}

	std::vector<cells::MapCell> RegionLifecycle::Update(const std::vector<cells::MapCell>& activeRegions, TimePoint now)
	{
		std::unordered_set<uint32_t> nowActive;
		nowActive.reserve(activeRegions.size());
		for (const cells::MapCell& region : activeRegions)
		{
			const uint32_t key = Key(region);
			if (!nowActive.insert(key).second)
				continue; // doublon
			if (m_active.count(key) == 0u)
			{
				m_tracker.OnPlayerEnter(ToGrid(region), now);
				m_known.insert(key);
				m_idle.erase(key);
			}
		}
		for (const uint32_t key : m_active)
		{
			if (nowActive.count(key) != 0u)
				continue;
			if (const std::optional<cells::MapCell> region = cells::MapCellFromZoneId(key))
				m_tracker.OnPlayerLeave(ToGrid(*region), now);
		}
		m_active = std::move(nowActive);
		m_tracker.Tick(now);

		std::vector<cells::MapCell> becameCold;
		for (const CellCoord& cell : m_tracker.CellsInState(GridState::Idle))
		{
			const cells::MapCell region = FromGrid(cell);
			if (m_idle.insert(Key(region)).second)
				becameCold.push_back(region);
		}
		// Une région passée directement en Removal (délais égaux) devient aussi froide.
		for (const CellCoord& cell : m_tracker.CellsInState(GridState::Removal))
		{
			const cells::MapCell region = FromGrid(cell);
			if (m_idle.insert(Key(region)).second)
				becameCold.push_back(region);
		}
		return becameCold;
	}

	bool RegionLifecycle::IsWarm(const cells::MapCell& region) const
	{
		if (m_known.count(Key(region)) == 0u)
			return false;
		const GridState state = m_tracker.StateOf(ToGrid(region));
		return state == GridState::Active || state == GridState::Loaded;
	}

	GridState RegionLifecycle::StateOf(const cells::MapCell& region) const
	{
		return m_tracker.StateOf(ToGrid(region));
	}

	std::vector<cells::MapCell> RegionLifecycle::DrainUnloaded()
	{
		std::vector<cells::MapCell> out;
		for (const CellCoord& cell : m_tracker.DrainRemovalCells())
		{
			const cells::MapCell region = FromGrid(cell);
			const uint32_t key = Key(region);
			m_known.erase(key);
			m_idle.erase(key);
			m_active.erase(key);
			out.push_back(region);
		}
		return out;
	}

	void RegionLifecycle::Clear()
	{
		m_tracker.Clear();
		m_known.clear();
		m_active.clear();
		m_idle.clear();
	}

	size_t RegionLifecycle::WarmCount() const
	{
		size_t n = 0;
		for (const uint32_t key : m_known)
		{
			if (const std::optional<cells::MapCell> region = cells::MapCellFromZoneId(key))
				n += IsWarm(*region) ? 1u : 0u;
		}
		return n;
	}
}
