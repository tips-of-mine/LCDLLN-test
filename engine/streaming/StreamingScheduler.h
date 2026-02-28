#pragma once

/**
 * @file StreamingScheduler.h
 * @brief Streaming scheduler with request/io/cpu/gpuUpload queues and priority (ring, distance, direction) (M10.1).
 *
 * Priority: Active > Visible > Far; in front of player > behind.
 * Producer: World update pushes chunk requests.
 */

#include "engine/world/World.h"

#include <cstdint>
#include <queue>
#include <vector>

namespace engine::streaming {

/**
 * @brief A chunk load request: chunk identifier and target ring state.
 */
struct ChunkRequest {
    ::engine::world::ChunkCoord chunkId{};
    ::engine::world::RingType   targetState = ::engine::world::RingType::Active;
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
 * @brief Streaming scheduler: request queue (priority) + io / cpu / gpuUpload queues.
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
                     double currentTime);

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

    /** @brief Pushes a request onto the IO queue (after request stage). */
    void PushToIoQueue(const ChunkRequest& req);

    /** @brief Pops one item from the IO queue. Returns false if empty. */
    bool PopFromIoQueue(ChunkRequest& out);

    /** @brief Pushes a request onto the CPU queue. */
    void PushToCpuQueue(const ChunkRequest& req);

    /** @brief Pops one item from the CPU queue. Returns false if empty. */
    bool PopFromCpuQueue(ChunkRequest& out);

    /** @brief Pushes a request onto the GPU upload queue. */
    void PushToGpuUploadQueue(const ChunkRequest& req);

    /** @brief Pops one item from the GPU upload queue. Returns false if empty. */
    bool PopFromGpuUploadQueue(ChunkRequest& out);

    [[nodiscard]] size_t IoQueueSize() const noexcept;
    [[nodiscard]] size_t CpuQueueSize() const noexcept;
    [[nodiscard]] size_t GpuUploadQueueSize() const noexcept;

private:
    std::vector<QueuedChunkRequest> m_requestQueue;
    std::queue<ChunkRequest>         m_ioQueue;
    std::queue<ChunkRequest>        m_cpuQueue;
    std::queue<ChunkRequest>        m_gpuUploadQueue;
};

} // namespace engine::streaming
