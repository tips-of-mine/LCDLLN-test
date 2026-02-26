#pragma once

/**
 * @file SystemAllocator.h
 * @brief Generic system allocator with per-tag tracking.
 *
 * Wraps platform-level aligned allocation (_aligned_malloc / std::aligned_alloc)
 * and maintains atomic statistics per MemTag so that DumpStats() is always
 * accurate even in multi-threaded contexts.
 *
 * API:
 *   void* SystemAllocator::Alloc(size, align, tag)
 *   void  SystemAllocator::Free (ptr,  size,  tag)
 *   const TagStats& SystemAllocator::GetStats(tag)
 */

#include "MemoryTags.h"

#include <atomic>
#include <array>
#include <cstddef>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// AtomicTagStats — lock-free per-tag counters
// ---------------------------------------------------------------------------

/// @brief Thread-safe statistics for one MemTag (internal storage).
struct AtomicTagStats {
    std::atomic<std::size_t> currentBytes{0};
    std::atomic<std::size_t> peakBytes{0};
    std::atomic<uint64_t>    allocCount{0};
    std::atomic<uint64_t>    freeCount{0};

    /// @brief Update currentBytes after an allocation of `bytes`.
    void RecordAlloc(std::size_t bytes) noexcept;

    /// @brief Update currentBytes after a free of `bytes`.
    void RecordFree(std::size_t bytes) noexcept;

    /// @brief Take a snapshot into a plain TagStats (non-atomic).
    [[nodiscard]] TagStats Snapshot() const noexcept;
};

// ---------------------------------------------------------------------------
// SystemAllocator
// ---------------------------------------------------------------------------

/// @brief Singleton-style system allocator: aligned alloc/free + tracking.
class SystemAllocator {
public:
    SystemAllocator()  = delete; ///< Static class; no instances.

    /**
     * @brief Allocate `size` bytes with `align` alignment, tagged with `tag`.
     *
     * @param size   Number of bytes to allocate (must be > 0).
     * @param align  Alignment in bytes (must be a power of two, >= 1).
     * @param tag    Subsystem tag for tracking.
     * @return Pointer to allocated memory; never null (asserts on failure).
     */
    static void* Alloc(std::size_t size, std::size_t align, MemTag tag) noexcept;

    /**
     * @brief Free memory previously allocated with Alloc.
     *
     * @param ptr   Pointer to memory (may be nullptr — no-op).
     * @param size  Size that was passed to Alloc (used to update stats).
     * @param tag   Same tag that was used for the matching Alloc.
     */
    static void Free(void* ptr, std::size_t size, MemTag tag) noexcept;

    /**
     * @brief Return a snapshot of statistics for the given tag.
     * @param tag Target subsystem tag.
     */
    [[nodiscard]] static TagStats GetStats(MemTag tag) noexcept;

    /**
     * @brief Return raw atomic stats for the given tag (for internal use).
     * @param tag Target subsystem tag.
     */
    [[nodiscard]] static AtomicTagStats& GetAtomicStats(MemTag tag) noexcept;

private:
    /// Per-tag atomic statistics; indexed by static_cast<uint8_t>(MemTag).
    static std::array<AtomicTagStats,
                      static_cast<std::size_t>(MemTag::Count)> s_stats;
};

} // namespace engine::core::memory
