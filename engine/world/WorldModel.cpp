#include "engine/world/WorldModel.h"

#include <algorithm>
#include <cmath>

namespace engine::world
{
	namespace
	{
		constexpr int kActiveRadius = 2;   // 5x5
		constexpr int kVisibleRadius = 3;   // 7x7
		constexpr int kFarRadius = 5;      // 11x11 (Far HLOD)
	}

	GlobalChunkCoord World::WorldToGlobalChunkCoord(float worldX, float worldZ)
	{
		const int32_t cx = static_cast<int32_t>(std::floor(worldX / static_cast<float>(kChunkSize)));
		const int32_t cz = static_cast<int32_t>(std::floor(worldZ / static_cast<float>(kChunkSize)));
		return GlobalChunkCoord{ cx, cz };
	}

	void World::GlobalToZoneAndLocal(GlobalChunkCoord g, ZoneCoord& zone, LocalChunkCoord& local)
	{
		zone.x = static_cast<int32_t>(std::floor(static_cast<float>(g.x) / static_cast<float>(kChunksPerZoneAxis)));
		zone.z = static_cast<int32_t>(std::floor(static_cast<float>(g.z) / static_cast<float>(kChunksPerZoneAxis)));
		local.x = g.x - zone.x * kChunksPerZoneAxis;
		local.z = g.z - zone.z * kChunksPerZoneAxis;
	}

	struct ChunkBounds World::ChunkBounds(ChunkCoord c)
	{
		struct ChunkBounds b;
		b.minX = static_cast<float>(c.x * kChunkSize);
		b.minZ = static_cast<float>(c.z * kChunkSize);
		b.maxX = static_cast<float>((c.x + 1) * kChunkSize);
		b.maxZ = static_cast<float>((c.z + 1) * kChunkSize);
		return b;
	}

	void World::Update(const engine::math::Vec3& playerPositionWorld)
	{
		const GlobalChunkCoord center = WorldToGlobalChunkCoord(playerPositionWorld.x, playerPositionWorld.z);

		// Hysteresis: only recompute when player has moved to a different chunk.
		if (m_hasLastCenter && center == m_lastCenterChunk)
			return;

		m_lastCenterChunk = center;
		m_hasLastCenter = true;
		m_pendingRequests.clear();

		// Build chunk set per ring: Active 5x5, Visible 7x7 (band), Far 11x11 (band).
		for (int dz = -kFarRadius; dz <= kFarRadius; ++dz)
		{
			for (int dx = -kFarRadius; dx <= kFarRadius; ++dx)
			{
				const int cx = center.x + dx;
				const int cz = center.z + dz;

				const int dist = std::max(std::abs(dx), std::abs(dz));
				ChunkRing ring;
				if (dist <= kActiveRadius)
					ring = ChunkRing::Active;
				else if (dist <= kVisibleRadius)
					ring = ChunkRing::Visible;
				else
					ring = ChunkRing::Far;

				m_pendingRequests.push_back(ChunkRequest{ GlobalChunkCoord{ cx, cz }, ring });
			}
		}
	}

	std::span<const ChunkRequest> World::GetPendingChunkRequests() const
	{
		return std::span<const ChunkRequest>(m_pendingRequests.data(), m_pendingRequests.size());
	}

	ChunkRing World::GetRingForChunk(GlobalChunkCoord chunk) const
	{
		if (!m_hasLastCenter)
			return ChunkRing::Far;
		const int dx = chunk.x - m_lastCenterChunk.x;
		const int dz = chunk.z - m_lastCenterChunk.z;
		const int dist = std::max(std::abs(dx), std::abs(dz));
		if (dist <= kActiveRadius)
			return ChunkRing::Active;
		if (dist <= kVisibleRadius)
			return ChunkRing::Visible;
		return ChunkRing::Far;
	}
}
