#include "engine/render/terrain_chunk/ChunkRuntime.h"

#include <algorithm>

namespace engine::render::terrain_chunk
{
	uint64_t ChunkRuntime::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void ChunkRuntime::Init(const Config& cfg)
	{
		m_cfg = cfg;
		m_slots.clear();
		m_lru.clear();
		m_coordToSlot.clear();
		m_nextSlotId = 1;
		m_residentBytes = 0;
	}

	ChunkSlotId ChunkRuntime::GetOrAllocateSlot(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_coordToSlot.find(key);
		if (it != m_coordToSlot.end()) return it->second;

		const ChunkSlotId id = m_nextSlotId++;
		Slot slot;
		slot.coord = coord;
		m_slots.emplace(id, slot);
		m_lru.push_front(id); // nouveau slot = most recent
		m_coordToSlot.emplace(key, id);
		return id;
	}

	void ChunkRuntime::AddResidentBytes(ChunkSlotId slot, size_t additionalBytes)
	{
		auto it = m_slots.find(slot);
		if (it == m_slots.end()) return;
		it->second.residentBytes += additionalBytes;
		it->second.residency = ChunkResidency::Resident;
		m_residentBytes += additionalBytes;
	}

	void ChunkRuntime::UpdateRing(engine::world::GlobalChunkCoord coord, engine::world::ChunkRing ring)
	{
		auto cit = m_coordToSlot.find(PackCoord(coord));
		if (cit == m_coordToSlot.end()) return;
		auto sit = m_slots.find(cit->second);
		if (sit == m_slots.end()) return;
		sit->second.ring = ring;
	}

	void ChunkRuntime::Touch(ChunkSlotId slot)
	{
		auto lit = std::find(m_lru.begin(), m_lru.end(), slot);
		if (lit == m_lru.end()) return;
		m_lru.erase(lit);
		m_lru.push_front(slot);
	}

	std::vector<ChunkSlotId> ChunkRuntime::CollectEvictionsForBudget()
	{
		std::vector<ChunkSlotId> evictions;
		// Itère depuis la fin (least recently used) et collecte les chunks
		// Far évincables jusqu'à respecter le budget.
		auto it = m_lru.rbegin();
		while (m_residentBytes > m_cfg.gpuBudgetBytes && it != m_lru.rend())
		{
			auto sit = m_slots.find(*it);
			if (sit == m_slots.end())
			{
				++it;
				continue;
			}
			// Skip si Active ou Visible (ring protégé).
			if (sit->second.ring == engine::world::ChunkRing::Active
			 || sit->second.ring == engine::world::ChunkRing::Visible)
			{
				++it;
				continue;
			}
			// Évincable.
			evictions.push_back(*it);
			m_residentBytes -= sit->second.residentBytes;
			sit->second.residentBytes = 0;
			++it;
		}
		return evictions;
	}

	void ChunkRuntime::RemoveSlot(ChunkSlotId slot)
	{
		auto sit = m_slots.find(slot);
		if (sit == m_slots.end()) return;
		// Si CollectEvictionsForBudget a déjà décrémenté, residentBytes = 0.
		// Sinon (cas remove direct sans eviction), on décrémente ici.
		m_residentBytes -= sit->second.residentBytes;
		m_coordToSlot.erase(PackCoord(sit->second.coord));
		m_slots.erase(sit);
		m_lru.remove(slot);
	}

	ChunkResidency ChunkRuntime::GetResidency(ChunkSlotId slot) const
	{
		auto it = m_slots.find(slot);
		return (it == m_slots.end()) ? ChunkResidency::Skipped : it->second.residency;
	}

	engine::world::ChunkRing ChunkRuntime::GetRingForSlot(ChunkSlotId slot) const
	{
		auto it = m_slots.find(slot);
		return (it == m_slots.end()) ? engine::world::ChunkRing::Far : it->second.ring;
	}

	engine::world::GlobalChunkCoord ChunkRuntime::GetCoordForSlot(ChunkSlotId slot) const
	{
		auto it = m_slots.find(slot);
		return (it == m_slots.end()) ? engine::world::GlobalChunkCoord{0, 0} : it->second.coord;
	}
}
