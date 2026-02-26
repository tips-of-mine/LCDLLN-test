/**
 * @file SystemAllocator.cpp
 * @brief SystemAllocator implementation: aligned alloc/free + per-tag tracking.
 */

#include "SystemAllocator.h"

#include <cassert>
#include <cstdlib>

#ifdef _WIN32
#   include <malloc.h>       // _aligned_malloc / _aligned_free
#endif

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------

std::array<AtomicTagStats, static_cast<std::size_t>(MemTag::Count)>
    SystemAllocator::s_stats{};

// ---------------------------------------------------------------------------
// AtomicTagStats helpers
// ---------------------------------------------------------------------------

void AtomicTagStats::RecordAlloc(std::size_t bytes) noexcept {
    allocCount.fetch_add(1u, std::memory_order_relaxed);

    const std::size_t prev =
        currentBytes.fetch_add(bytes, std::memory_order_relaxed);
    const std::size_t newCurrent = prev + bytes;

    // Update peak with a compare-exchange loop.
    std::size_t observed = peakBytes.load(std::memory_order_relaxed);
    while (newCurrent > observed) {
        if (peakBytes.compare_exchange_weak(observed, newCurrent,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
            break;
        }
    }
}

void AtomicTagStats::RecordFree(std::size_t bytes) noexcept {
    freeCount.fetch_add(1u, std::memory_order_relaxed);
    currentBytes.fetch_sub(bytes, std::memory_order_relaxed);
}

TagStats AtomicTagStats::Snapshot() const noexcept {
    TagStats s{};
    s.currentBytes = currentBytes.load(std::memory_order_relaxed);
    s.peakBytes    = peakBytes.load(std::memory_order_relaxed);
    s.allocCount   = allocCount.load(std::memory_order_relaxed);
    s.freeCount    = freeCount.load(std::memory_order_relaxed);
    return s;
}

// ---------------------------------------------------------------------------
// SystemAllocator::Alloc
// ---------------------------------------------------------------------------

void* SystemAllocator::Alloc(std::size_t size,
                              std::size_t align,
                              MemTag      tag) noexcept {
    assert(size  > 0 && "SystemAllocator::Alloc: size must be > 0");
    assert(align > 0 && (align & (align - 1u)) == 0u &&
           "SystemAllocator::Alloc: align must be a power of two");

    // Minimum alignment required by malloc family is sizeof(void*).
    const std::size_t effectiveAlign = align < sizeof(void*) ? sizeof(void*) : align;

    void* ptr = nullptr;

#ifdef _WIN32
    ptr = _aligned_malloc(size, effectiveAlign);
#else
    // POSIX: aligned_alloc requires size to be a multiple of alignment.
    const std::size_t adjustedSize =
        (size + effectiveAlign - 1u) & ~(effectiveAlign - 1u);
    ptr = std::aligned_alloc(effectiveAlign, adjustedSize);
#endif

    assert(ptr != nullptr && "SystemAllocator::Alloc: out of memory");

    GetAtomicStats(tag).RecordAlloc(size);
    return ptr;
}

// ---------------------------------------------------------------------------
// SystemAllocator::Free
// ---------------------------------------------------------------------------

void SystemAllocator::Free(void* ptr, std::size_t size, MemTag tag) noexcept {
    if (ptr == nullptr) {
        return; // no-op on null, consistent with standard free()
    }

#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif

    GetAtomicStats(tag).RecordFree(size);
}

// ---------------------------------------------------------------------------
// SystemAllocator::GetStats / GetAtomicStats
// ---------------------------------------------------------------------------

TagStats SystemAllocator::GetStats(MemTag tag) noexcept {
    return GetAtomicStats(tag).Snapshot();
}

AtomicTagStats& SystemAllocator::GetAtomicStats(MemTag tag) noexcept {
    const auto idx = static_cast<std::size_t>(tag);
    assert(idx < static_cast<std::size_t>(MemTag::Count));
    return s_stats[idx];
}

} // namespace engine::core::memory
