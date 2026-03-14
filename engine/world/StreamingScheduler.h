#pragma once

#include "engine/math/Math.h"
#include "engine/world/WorldModel.h"
#include "engine/world/ChunkPackageLayout.h"

#include <cstdint>
#include <optional>
#include <queue>
#include <span>
#include <vector>

namespace engine::world
{
	class StreamCache;
	/// Job carried through IO/CPU/GPU queues; carries stream version for drop-mismatch (M10.2).
	/// Segment (M10.5) indicates which chunk package file to load (geo/tex/instances/nav/probes); load in GetChunkLoadOrder() order.
	struct ChunkJob
	{
		ChunkCoord coord{ 0, 0 };
		uint32_t streamVersion = 0;
		ChunkSegment segment = ChunkSegment::Geo;
	};

	/// Computes streaming priority score for a chunk (higher = load first).
	/// Order: Active > Visible > Far; in front of player > behind (XZ plane).
	/// \param chunkId Chunk coordinate.
	/// \param ring Ring (Active/Visible/Far).
	/// \param playerPositionZoneLocal Player position in zone-local meters.
	/// \param playerForwardXZ Forward direction in XZ (e.g. from camera), used for "in front" test.
	/// \return Priority value; higher means higher priority.
	uint32_t ComputeChunkPriority(ChunkCoord chunkId, ChunkRing ring,
	                              const engine::math::Vec3& playerPositionZoneLocal,
	                              const engine::math::Vec3& playerForwardXZ);

	/// Streaming scheduler: request queue with priorities, plus io/cpu/gpuUpload queues (M10.1).
	/// Producer: World update pushes requests; scheduler assigns priority and exposes sorted list.
	class StreamingScheduler
	{
	public:
		StreamingScheduler() = default;

		/// Pushes requests from World, assigns priority from ring and player position/direction, stores in request queue (sorted).
		/// \param requests Pending chunk requests (e.g. from World::GetPendingChunkRequests()).
		/// \param playerPositionZoneLocal Player position in zone-local meters.
		/// \param playerForwardXZ Forward direction in XZ (normalized or not); used for "in front" boost.
		void PushRequests(std::span<const ChunkRequest> requests,
		                  const engine::math::Vec3& playerPositionZoneLocal,
		                  const engine::math::Vec3& playerForwardXZ);

		/// Returns the current request queue as a span, ordered by priority (highest first).
		std::span<const ChunkRequest> GetPrioritizedRequests() const;

		/// Sets the stream cache for IO stage: hit cache -> skip disk (M10.3).
		void SetStreamCache(StreamCache* cache) { m_streamCache = cache; }
		/// Returns cached blob for \p key if cache is set and hit; otherwise nullopt. Use in IO stage before disk read.
		std::optional<std::vector<uint8_t>> TryGetCachedBlob(std::string_view key) const;

		/// Current stream version; incremented each PushRequests. Jobs with version != this are stale (M10.2).
		uint32_t GetCurrentStreamVersion() const { return m_streamVersion; }

		/// Drops jobs whose streamVersion != current from the IO queue; counts dropped in cancelled stats.
		void DropStaleFromIoQueue();
		/// Drops jobs whose streamVersion != current from the CPU queue; counts dropped in cancelled stats.
		void DropStaleFromCpuQueue();
		/// Drops jobs whose streamVersion != current from the GPU upload queue; counts dropped in cancelled stats.
		void DropStaleFromGpuUploadQueue();
		/// Drops stale jobs from all three stage queues (IO, CPU, GPU upload). Call once per frame before processing.
		void DropStaleFromAllQueues();

		/// Clears queues and request state for a server-authoritative zone transition on the client.
		void ResetForZoneChange();

		/// Returns total number of jobs dropped as stale (cancelled) since last reset.
		uint32_t GetCancelledJobCount() const { return m_cancelledJobCount; }
		/// Resets the cancelled-job counter (e.g. for stats reporting).
		void ResetCancelledJobCount() { m_cancelledJobCount = 0; }

		/// Pushes one chunk as segment jobs to the IO queue in load order (geo, tex, instances, nav, probes) for progressive loading (M10.5).
		void PushChunkToIoQueueInLoadOrder(ChunkCoord coord);

		/// Request queue (priority-ordered); consumer may read for draw list / loading.
		const std::vector<ChunkRequest>& GetRequestQueue() const { return m_requestQueue; }
		/// IO queue; jobs carry streamVersion and segment for drop-mismatch and load order (M10.2, M10.5).
		std::queue<ChunkJob>& GetIoQueue() { return m_ioQueue; }
		const std::queue<ChunkJob>& GetIoQueue() const { return m_ioQueue; }
		/// CPU queue; jobs carry streamVersion for drop-mismatch (M10.2).
		std::queue<ChunkJob>& GetCpuQueue() { return m_cpuQueue; }
		const std::queue<ChunkJob>& GetCpuQueue() const { return m_cpuQueue; }
		/// GPU upload queue; jobs carry streamVersion for drop-mismatch (M10.2).
		std::queue<ChunkJob>& GetGpuUploadQueue() { return m_gpuUploadQueue; }
		const std::queue<ChunkJob>& GetGpuUploadQueue() const { return m_gpuUploadQueue; }

	private:
		StreamCache* m_streamCache = nullptr;
		std::vector<ChunkRequest> m_requestQueue;
		std::queue<ChunkJob> m_ioQueue;
		std::queue<ChunkJob> m_cpuQueue;
		std::queue<ChunkJob> m_gpuUploadQueue;
		uint32_t m_streamVersion = 0;
		uint32_t m_cancelledJobCount = 0;
	};
}
