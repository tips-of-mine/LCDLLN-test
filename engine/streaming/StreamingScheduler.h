#pragma once

/**
 * @file StreamingScheduler.h
 * @brief Streaming scheduler with request/io/cpu/gpuUpload queues and priority (ring, distance, direction) (M10.1).
 *
 * Priority: Active > Visible > Far; in front of player > behind.
 * Producer: World update pushes chunk requests.
 */

#include "engine/world/World.h"
#include "engine/streaming/LruCache.h"
#include "engine/streaming/ChunkMeta.h"

#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace engine::streaming {

/** @brief Hash for ChunkCoord for use in unordered_map (version store). */
struct ChunkCoordHash {
    std::size_t operator()(const ::engine::world::ChunkCoord& c) const noexcept {
        std::size_t h = 0;
        h ^= static_cast<std::size_t>(static_cast<uint32_t>(c.zoneX)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(static_cast<uint32_t>(c.zoneZ)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(static_cast<uint32_t>(c.chunkX)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(static_cast<uint32_t>(c.chunkZ)) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief A chunk load request: chunk identifier, target ring state, stream version, and asset type (M10.2, M10.5).
 *
 * streamVersion is set when the request is pushed; at each stage (IO/CPU/GPU)
 * the consumer must check IsVersionCurrent() and drop (RecordCancelled) if mismatch.
 * assetType orders IO load priority: Geo first, then Tex, then Instances/Nav/Probes.
 */
struct ChunkRequest {
    ::engine::world::ChunkCoord chunkId{};
    ::engine::world::RingType   targetState = ::engine::world::RingType::Active;
    uint32_t                    streamVersion = 0u;
    ChunkAssetType              assetType = ChunkAssetType::Geo;
};

/**
 * @brief Queued request with enqueue time for aging/boost (avoid starvation).
 */
struct QueuedChunkRequest {
    ChunkRequest request{};
    double       enqueueTime = 0.0;
};

/**
 * @brief Computes priority score for a chunk request.
 *
 * Higher return value = higher priority.
 * Ordering: Active > Visible > Far; in front of player > behind.
 * Optional aging boost: requests waiting longer get a higher score component.
 *
 * @param request     Chunk request (chunkId, targetState).
 * @param playerX     Player world X (meters).
 * @param playerZ     Player world Z (meters).
 * @param viewDirX    View direction X (normalized, typically forward).
 * @param viewDirZ    View direction Z (normalized).
 * @param enqueueTime Time when the request was enqueued (for aging).
 * @param currentTime Current time (same units as enqueueTime).
 * @return Priority score (higher = load first).
 */
float ComputeChunkPriority(const ChunkRequest& request,
                           float playerX, float playerZ,
                           float viewDirX, float viewDirZ,
                           double enqueueTime, double currentTime);

/**
 * @brief Streaming scheduler: request queue (priority) + io / cpu / gpuUpload queues + per-chunk versioning (M10.2).
 */
class StreamingScheduler {
public:
    StreamingScheduler() = default;

    /**
     * @brief Pushes a chunk request onto the request queue with computed priority.
     *
     * Called by World update (producer). Uses currentTime as enqueue time for aging.
     *
     * @param chunkId    Chunk to load.
     * @param targetState Ring (Active/Visible/Far) for this request.
     * @param playerX    Player world X.
     * @param playerZ    Player world Z.
     * @param viewDirX   View direction X (normalized).
     * @param viewDirZ   View direction Z (normalized).
     * @param currentTime Current time (e.g. engine time).
     */
    void PushRequest(::engine::world::ChunkCoord chunkId,
                     ::engine::world::RingType targetState,
                     float playerX, float playerZ,
                     float viewDirX, float viewDirZ,
                     double currentTime,
                     ChunkAssetType assetType = ChunkAssetType::Geo);

    /**
     * @brief Pops the highest-priority request from the request queue.
     *
     * Priority is recomputed with current time (aging). Returns false if queue empty.
     *
     * @param playerX     Player world X (for priority).
     * @param playerZ     Player world Z.
     * @param viewDirX    View direction X.
     * @param viewDirZ    View direction Z.
     * @param currentTime Current time (for aging).
     * @param out         Filled with the popped request.
     * @return true if a request was popped, false if request queue empty.
     */
    bool PopNextRequest(float playerX, float playerZ, float viewDirX, float viewDirZ,
                        double currentTime, ChunkRequest& out);

    /** @brief Request queue size (priority-ordered). */
    [[nodiscard]] size_t RequestQueueSize() const noexcept { return m_requestQueue.size(); }

    /** @brief Sets the LRU cache used by the IO stage (hit cache -> skip disk). May be null. */
    void SetCache(LruCache* cache) noexcept { m_cache = cache; }

    /** @brief Returns cached blob for key if present (IO stage: hit -> skip disk). */
    [[nodiscard]] std::optional<std::pair<const void*, size_t>> GetCachedBlob(const std::string& key) const;

    /** @brief Pushes a request onto the IO queue (after request stage). */
    void PushToIoQueue(const ChunkRequest& req);

    /** @brief Pops one item from the IO queue. Returns false if empty. */
    bool PopFromIoQueue(ChunkRequest& out);

    /**
     * @brief Pops from IO and, if version still current, pushes to CPU; otherwise drops and records cancelled.
     * @return true if one item was consumed (either advanced or dropped), false if IO queue empty.
     */
    bool TryAdvanceFromIoToCpu();

    /** @brief Pushes a request onto the CPU queue. */
    void PushToCpuQueue(const ChunkRequest& req);

    /** @brief Pops one item from the CPU queue. Returns false if empty. */
    bool PopFromCpuQueue(ChunkRequest& out);

    /**
     * @brief Pops from CPU and, if version still current, pushes to GPU upload; otherwise drops and records cancelled.
     * @return true if one item was consumed (either advanced or dropped), false if CPU queue empty.
     */
    bool TryAdvanceFromCpuToGpuUpload();

    /** @brief Pushes a request onto the GPU upload queue. */
    void PushToGpuUploadQueue(const ChunkRequest& req);

    /** @brief Pops one item from the GPU upload queue. Returns false if empty. */
    bool PopFromGpuUploadQueue(ChunkRequest& out);

    [[nodiscard]] size_t IoQueueSize() const noexcept;
    [[nodiscard]] size_t CpuQueueSize() const noexcept;
    [[nodiscard]] size_t GpuUploadQueueSize() const noexcept;

    /**
     * @brief Returns true if the given streamVersion is still the current version for that chunk.
     *
     * Call after popping from IO/CPU/GPU before pushing to the next stage; if false, drop and call RecordCancelled().
     */
    [[nodiscard]] bool IsVersionCurrent(::engine::world::ChunkCoord chunkId, uint32_t streamVersion) const;

    /** @brief Records one dropped job (version mismatch). */
    void RecordCancelled() noexcept;

    /** @brief Returns the number of jobs dropped due to version mismatch (cancelled stats). */
    [[nodiscard]] uint32_t GetCancelledCount() const noexcept { return m_cancelledCount; }

private:
    std::vector<QueuedChunkRequest> m_requestQueue;
    std::queue<ChunkRequest>         m_ioQueue;
    std::queue<ChunkRequest>        m_cpuQueue;
    std::queue<ChunkRequest>        m_gpuUploadQueue;
    std::unordered_map<::engine::world::ChunkCoord, uint32_t, ChunkCoordHash> m_chunkVersion;
    uint32_t m_cancelledCount = 0u;
    LruCache* m_cache = nullptr;
};

} // namespace engine::streaming
