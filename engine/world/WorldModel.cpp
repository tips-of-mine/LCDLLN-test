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

	ChunkCoord World::WorldToChunkCoord(float worldX, float worldZ)
	{
		const int cx = static_cast<int>(std::floor(worldX / static_cast<float>(kChunkSize)));
		const int cz = static_cast<int>(std::floor(worldZ / static_cast<float>(kChunkSize)));
		const int clampedX = std::clamp(cx, 0, kChunksPerZoneAxis - 1);
		const int clampedZ = std::clamp(cz, 0, kChunksPerZoneAxis - 1);
		return ChunkCoord{ clampedX, clampedZ };
	}

	engine::world::ChunkBounds World::ChunkBounds(ChunkCoord c)
	{
		ChunkBounds b;
		b.minX = static_cast<float>(c.x * kChunkSize);
		b.minZ = static_cast<float>(c.z * kChunkSize);
		b.maxX = static_cast<float>((c.x + 1) * kChunkSize);
		b.maxZ = static_cast<float>((c.z + 1) * kChunkSize);
		return b;
	}

	void World::Update(const engine::math::Vec3& playerPositionZoneLocal)
	{
		const ChunkCoord center = WorldToChunkCoord(playerPositionZoneLocal.x, playerPositionZoneLocal.z);

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
				if (cx < 0 || cx >= kChunksPerZoneAxis || cz < 0 || cz >= kChunksPerZoneAxis)
					continue;

				const int dist = std::max(std::abs(dx), std::abs(dz));
				ChunkRing ring;
				if (dist <= kActiveRadius)
					ring = ChunkRing::Active;
				else if (dist <= kVisibleRadius)
					ring = ChunkRing::Visible;
				else
					ring = ChunkRing::Far;

				m_pendingRequests.push_back(ChunkRequest{ ChunkCoord{ cx, cz }, ring });
			}
		}
	}

	std::span<const ChunkRequest> World::GetPendingChunkRequests() const
	{
		return std::span<const ChunkRequest>(m_pendingRequests.data(), m_pendingRequests.size());
	}

	ChunkRing World::GetRingForChunk(ChunkCoord chunk) const
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
