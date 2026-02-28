/**
 * @file StreamingScheduler.cpp
 * @brief Streaming scheduler: priority function and queues (M10.1).
 */

#include "engine/streaming/StreamingScheduler.h"
#include "engine/core/Config.h"
#include "engine/world/World.h"

#include <cmath>
#include <algorithm>

namespace engine::streaming {

namespace {

/** Base priority by ring: Active > Visible > Far. */
float RingBasePriority(::engine::world::RingType ring) {
    switch (ring) {
        case ::engine::world::RingType::Active:  return 1000.0f;
        case ::engine::world::RingType::Visible: return 100.0f;
        case ::engine::world::RingType::Far:     return 1.0f;
    }
    return 0.0f;
}

/** Direction factor: chunk in front of player gets positive boost. */
float DirectionFactor(float playerX, float playerZ, float viewDirX, float viewDirZ,
                     const ::engine::world::ChunkCoord& chunkId) {
    ::engine::world::ChunkBoundsResult b = ::engine::world::ChunkBounds(chunkId);
    float cx = (b.minX + b.maxX) * 0.5f;
    float cz = (b.minZ + b.maxZ) * 0.5f;
    float dx = cx - playerX;
    float dz = cz - playerZ;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-6f) return 50.0f;
    float toChunkX = dx / len;
    float toChunkZ = dz / len;
    float dot = toChunkX * viewDirX + toChunkZ * viewDirZ;
    return dot * 50.0f;
}

/** Aging boost per second to avoid starvation. */
constexpr float kAgingBoostPerSecond = 10.0f;

} // namespace

float ComputeChunkPriority(const ChunkRequest& request,
                           float playerX, float playerZ,
                           float viewDirX, float viewDirZ,
                           double enqueueTime, double currentTime) {
    float base = RingBasePriority(request.targetState);
    float dir  = DirectionFactor(playerX, playerZ, viewDirX, viewDirZ, request.chunkId);
    float age  = static_cast<float>(currentTime - enqueueTime) * kAgingBoostPerSecond;
    float agingMax = ::engine::core::Config::GetFloat("streaming.aging_boost_max", 500.0f);
    if (age > agingMax) age = agingMax;
    return base + dir + age;
}

void StreamingScheduler::PushRequest(::engine::world::ChunkCoord chunkId,
                                     ::engine::world::RingType targetState,
                                     float playerX, float playerZ,
                                     float viewDirX, float viewDirZ,
                                     double currentTime) {
    uint32_t& v = m_chunkVersion[chunkId];
    v = (v == 0xFFFFFFFFu) ? 1u : (v + 1u);
    QueuedChunkRequest q;
    q.request.chunkId = chunkId;
    q.request.targetState = targetState;
    q.request.streamVersion = v;
    q.enqueueTime = currentTime;
    m_requestQueue.push_back(q);
}

bool StreamingScheduler::PopNextRequest(float playerX, float playerZ, float viewDirX, float viewDirZ,
                                        double currentTime, ChunkRequest& out) {
    if (m_requestQueue.empty()) return false;
    auto it = std::max_element(m_requestQueue.begin(), m_requestQueue.end(),
        [this, playerX, playerZ, viewDirX, viewDirZ, currentTime](const QueuedChunkRequest& a, const QueuedChunkRequest& b) {
            float pa = ComputeChunkPriority(a.request, playerX, playerZ, viewDirX, viewDirZ, a.enqueueTime, currentTime);
            float pb = ComputeChunkPriority(b.request, playerX, playerZ, viewDirX, viewDirZ, b.enqueueTime, currentTime);
            return pa < pb;
        });
    out = it->request;
    *it = m_requestQueue.back();
    m_requestQueue.pop_back();
    return true;
}

void StreamingScheduler::PushToIoQueue(const ChunkRequest& req) {
    m_ioQueue.push(req);
}

bool StreamingScheduler::PopFromIoQueue(ChunkRequest& out) {
    if (m_ioQueue.empty()) return false;
    out = m_ioQueue.front();
    m_ioQueue.pop();
    return true;
}

bool StreamingScheduler::TryAdvanceFromIoToCpu() {
    if (m_ioQueue.empty()) return false;
    ChunkRequest req = m_ioQueue.front();
    m_ioQueue.pop();
    if (!IsVersionCurrent(req.chunkId, req.streamVersion)) {
        RecordCancelled();
        return true;
    }
    m_cpuQueue.push(req);
    return true;
}

void StreamingScheduler::PushToCpuQueue(const ChunkRequest& req) {
    m_cpuQueue.push(req);
}

bool StreamingScheduler::PopFromCpuQueue(ChunkRequest& out) {
    if (m_cpuQueue.empty()) return false;
    out = m_cpuQueue.front();
    m_cpuQueue.pop();
    return true;
}

bool StreamingScheduler::TryAdvanceFromCpuToGpuUpload() {
    if (m_cpuQueue.empty()) return false;
    ChunkRequest req = m_cpuQueue.front();
    m_cpuQueue.pop();
    if (!IsVersionCurrent(req.chunkId, req.streamVersion)) {
        RecordCancelled();
        return true;
    }
    m_gpuUploadQueue.push(req);
    return true;
}

void StreamingScheduler::PushToGpuUploadQueue(const ChunkRequest& req) {
    m_gpuUploadQueue.push(req);
}

bool StreamingScheduler::PopFromGpuUploadQueue(ChunkRequest& out) {
    if (m_gpuUploadQueue.empty()) return false;
    out = m_gpuUploadQueue.front();
    m_gpuUploadQueue.pop();
    return true;
}

size_t StreamingScheduler::IoQueueSize() const noexcept {
    return m_ioQueue.size();
}

size_t StreamingScheduler::CpuQueueSize() const noexcept {
    return m_cpuQueue.size();
}

size_t StreamingScheduler::GpuUploadQueueSize() const noexcept {
    return m_gpuUploadQueue.size();
}

bool StreamingScheduler::IsVersionCurrent(::engine::world::ChunkCoord chunkId, uint32_t streamVersion) const {
    auto it = m_chunkVersion.find(chunkId);
    return it != m_chunkVersion.end() && it->second == streamVersion;
}

void StreamingScheduler::RecordCancelled() noexcept {
    ++m_cancelledCount;
}

} // namespace engine::streaming
