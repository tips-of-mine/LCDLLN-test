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

	uint32_t RegionLifecycle::Key(cells::CellCoord region)
	{
		// n, e ∈ [−500, 500[ dans la carte du lore : 16 bits chacun suffisent.
		return (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(region.n))) << 16)
			| static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(region.e)));
	}

	CellCoord RegionLifecycle::ToGrid(cells::CellCoord region)
	{
		CellCoord c{};
		c.x = static_cast<int16_t>(region.e);
		c.z = static_cast<int16_t>(region.n);
		return c;
	}

	cells::CellCoord RegionLifecycle::FromGrid(CellCoord cell)
	{
		cells::CellCoord r{};
		r.n = cell.z;
		r.e = cell.x;
		return r;
	}

	std::vector<cells::CellCoord> RegionLifecycle::Update(const std::vector<cells::CellCoord>& activeRegions, TimePoint now)
	{
		std::unordered_set<uint32_t> nowActive;
		nowActive.reserve(activeRegions.size());
		for (const cells::CellCoord& region : activeRegions)
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
			cells::CellCoord region{};
			region.n = static_cast<int16_t>(static_cast<uint16_t>(key >> 16));
			region.e = static_cast<int16_t>(static_cast<uint16_t>(key & 0xFFFFu));
			m_tracker.OnPlayerLeave(ToGrid(region), now);
		}
		m_active = std::move(nowActive);
		m_tracker.Tick(now);

		std::vector<cells::CellCoord> becameCold;
		for (const CellCoord& cell : m_tracker.CellsInState(GridState::Idle))
		{
			const cells::CellCoord region = FromGrid(cell);
			if (m_idle.insert(Key(region)).second)
				becameCold.push_back(region);
		}
		// Une région passée directement en Removal (délais égaux) devient aussi froide.
		for (const CellCoord& cell : m_tracker.CellsInState(GridState::Removal))
		{
			const cells::CellCoord region = FromGrid(cell);
			if (m_idle.insert(Key(region)).second)
				becameCold.push_back(region);
		}
		return becameCold;
	}

	bool RegionLifecycle::IsWarm(cells::CellCoord region) const
	{
		if (m_known.count(Key(region)) == 0u)
			return false;
		const GridState state = m_tracker.StateOf(ToGrid(region));
		return state == GridState::Active || state == GridState::Loaded;
	}

	GridState RegionLifecycle::StateOf(cells::CellCoord region) const
	{
		return m_tracker.StateOf(ToGrid(region));
	}

	std::vector<cells::CellCoord> RegionLifecycle::DrainUnloaded()
	{
		std::vector<cells::CellCoord> out;
		for (const CellCoord& cell : m_tracker.DrainRemovalCells())
		{
			const cells::CellCoord region = FromGrid(cell);
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
			cells::CellCoord region{};
			region.n = static_cast<int16_t>(static_cast<uint16_t>(key >> 16));
			region.e = static_cast<int16_t>(static_cast<uint16_t>(key & 0xFFFFu));
			n += IsWarm(region) ? 1u : 0u;
		}
		return n;
	}
}
