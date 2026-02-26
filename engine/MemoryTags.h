#pragma once
// engine/core/memory/MemoryTags.h
// M00.2 — Memory tags: enum + per-tag statistics.
// Each subsystem uses a distinct tag so allocations can be tracked
// and reported independently (see Memory::DumpStats()).

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace engine::core::memory {

/// @brief Identifies which subsystem owns an allocation.
/// Tags map 1-to-1 to a MemTagStats slot; keep them contiguous.
enum class MemTag : uint8_t {
    Core    = 0, ///< Engine core (logging, config, time…)
    Render  = 1, ///< Render backend (Vulkan resources)
    Assets  = 2, ///< Asset pipeline (textures, meshes, shaders)
    World   = 3, ///< World / scene (entities, chunks)
    Net     = 4, ///< Network (packet buffers, session data)
    UI      = 5, ///< UI layer
    Tools   = 6, ///< Developer tools / profiler
    Temp    = 7, ///< Short-lived scratch allocations
    COUNT   = 8  ///< Sentinel – number of valid tags
};

/// @brief Per-tag tracking statistics.
/// All fields are updated atomically for thread-safety.
struct MemTagStats {
    std::atomic<int64_t>  currentBytes{0};  ///< Bytes currently allocated
    std::atomic<int64_t>  peakBytes{0};     ///< Highest ever currentBytes
    std::atomic<uint64_t> allocCount{0};    ///< Total alloc() calls
    std::atomic<uint64_t> freeCount{0};     ///< Total free() calls

    /// Record an allocation of @p bytes for this tag.
    void recordAlloc(int64_t bytes) noexcept {
        int64_t cur = currentBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        allocCount.fetch_add(1, std::memory_order_relaxed);
        // Update peak (best-effort; racy but conservative)
        int64_t peak = peakBytes.load(std::memory_order_relaxed);
        while (cur > peak &&
               !peakBytes.compare_exchange_weak(peak, cur,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    /// Record a free of @p bytes for this tag.
    void recordFree(int64_t bytes) noexcept {
        currentBytes.fetch_sub(bytes, std::memory_order_relaxed);
        freeCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Non-copyable: atomics cannot be copied.
    MemTagStats() = default;
    MemTagStats(const MemTagStats&) = delete;
    MemTagStats& operator=(const MemTagStats&) = delete;
};

} // namespace engine::core::memory
