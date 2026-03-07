#include "engine/world/StreamingScheduler.h"
#include "engine/world/StreamCache.h"
#include "engine/world/ChunkPackageLayout.h"

#include <algorithm>

namespace engine::world
{
	namespace
	{
		/// Ring order for priority: Active=3, Visible=2, Far=1 (higher = load first).
		uint32_t RingToBasePriority(ChunkRing ring)
		{
			switch (ring)
			{
			case ChunkRing::Active:  return 3u;
			case ChunkRing::Visible: return 2u;
			case ChunkRing::Far:     return 1u;
			}
			return 0u;
		}
	}

	uint32_t ComputeChunkPriority(ChunkCoord chunkId, ChunkRing ring,
	                              const engine::math::Vec3& playerPositionZoneLocal,
	                              const engine::math::Vec3& playerForwardXZ)
	{
		const uint32_t base = RingToBasePriority(ring);
		// In front of player (XZ): chunk center vs player position, dot with forward XZ > 0.
		const float centerX = (static_cast<float>(chunkId.x) + 0.5f) * static_cast<float>(kChunkSize);
		const float centerZ = (static_cast<float>(chunkId.z) + 0.5f) * static_cast<float>(kChunkSize);
		const float toChunkX = centerX - playerPositionZoneLocal.x;
		const float toChunkZ = centerZ - playerPositionZoneLocal.z;
		const float dotXZ = toChunkX * playerForwardXZ.x + toChunkZ * playerForwardXZ.z;
		const uint32_t inFront = (dotXZ > 0.0f) ? 1u : 0u;
		// Score: base*2 + inFront => 7,6,5,4,3,2,1 (higher first).
		return base * 2u + inFront;
	}

	void StreamingScheduler::PushRequests(std::span<const ChunkRequest> requests,
	                                      const engine::math::Vec3& playerPositionZoneLocal,
	                                      const engine::math::Vec3& playerForwardXZ)
	{
		// M10.2: each request batch gets a new stream version; jobs carry it for drop-mismatch.
		++m_streamVersion;
		m_requestQueue.clear();
		m_requestQueue.reserve(requests.size());
		for (const ChunkRequest& r : requests)
		{
			ChunkRequest req = r;
			req.priority = ComputeChunkPriority(r.chunkId, r.targetState, playerPositionZoneLocal, playerForwardXZ);
			req.streamVersion = m_streamVersion;
			m_requestQueue.push_back(req);
		}
		// Sort by priority descending (highest first).
		std::sort(m_requestQueue.begin(), m_requestQueue.end(),
		          [](const ChunkRequest& a, const ChunkRequest& b) { return a.priority > b.priority; });
	}

	namespace
	{
		/// Drains queue, drops jobs with version != currentVersion; returns count dropped.
		void DropStaleFromQueue(std::queue<ChunkJob>& queue, uint32_t currentVersion, uint32_t& cancelledCount)
		{
			std::queue<ChunkJob> kept;
			while (!queue.empty())
			{
				ChunkJob job = queue.front();
				queue.pop();
				if (job.streamVersion == currentVersion)
					kept.push(std::move(job));
				else
					++cancelledCount;
			}
			queue = std::move(kept);
		}
	}

	void StreamingScheduler::DropStaleFromIoQueue()
	{
		DropStaleFromQueue(m_ioQueue, m_streamVersion, m_cancelledJobCount);
	}

	void StreamingScheduler::DropStaleFromCpuQueue()
	{
		DropStaleFromQueue(m_cpuQueue, m_streamVersion, m_cancelledJobCount);
	}

	void StreamingScheduler::DropStaleFromGpuUploadQueue()
	{
		DropStaleFromQueue(m_gpuUploadQueue, m_streamVersion, m_cancelledJobCount);
	}

	void StreamingScheduler::DropStaleFromAllQueues()
	{
		DropStaleFromIoQueue();
		DropStaleFromCpuQueue();
		DropStaleFromGpuUploadQueue();
	}

	std::span<const ChunkRequest> StreamingScheduler::GetPrioritizedRequests() const
	{
		return std::span<const ChunkRequest>(m_requestQueue.data(), m_requestQueue.size());
	}

	std::optional<std::vector<uint8_t>> StreamingScheduler::TryGetCachedBlob(std::string_view key) const
	{
		if (m_streamCache == nullptr)
			return std::nullopt;
		return m_streamCache->Lookup(key);
	}

	void StreamingScheduler::PushChunkToIoQueueInLoadOrder(ChunkCoord coord)
	{
		const uint32_t version = m_streamVersion;
		const std::array<ChunkSegment, kChunkSegmentCount> order = GetChunkLoadOrder();
		for (ChunkSegment seg : order)
		{
			ChunkJob job;
			job.coord = coord;
			job.streamVersion = version;
			job.segment = seg;
			m_ioQueue.push(job);
		}
	}
}
