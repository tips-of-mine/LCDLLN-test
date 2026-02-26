// engine/core/memory/SystemAllocator.cpp
// M00.2 — SystemAllocator implementation.

#include "SystemAllocator.h"

#include <cassert>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#   include <malloc.h> // _aligned_malloc / _aligned_free
#endif

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Internal globals
// ---------------------------------------------------------------------------

/// Global per-tag stats. Outlives all allocations.
static MemTagStats g_tagStats[static_cast<size_t>(MemTag::COUNT)];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Platform-specific aligned allocation (non-throwing).
static void* alignedAlloc(size_t size, size_t alignment) noexcept {
    assert(size > 0);
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0); // power-of-two
#ifdef _WIN32
    return ::_aligned_malloc(size, alignment);
#else
    // POSIX: alignment must also be a multiple of sizeof(void*) and size ≥ alignment.
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = nullptr;
    if (::posix_memalign(&ptr, alignment, size) != 0) ptr = nullptr;
    return ptr;
#endif
}

/// Platform-specific aligned free.
static void alignedFree(void* ptr) noexcept {
#ifdef _WIN32
    ::_aligned_free(ptr);
#else
    ::free(ptr);
#endif
}

// ---------------------------------------------------------------------------
// SystemAllocator API
// ---------------------------------------------------------------------------

void* SystemAllocator::alloc(size_t size, size_t alignment, MemTag tag) {
    assert(size > 0);
    void* ptr = alignedAlloc(size, alignment);
    // OOM is fatal – assert immediately rather than propagating nullptr.
    assert(ptr != nullptr && "SystemAllocator::alloc – out of memory");

    const auto idx = static_cast<size_t>(tag);
    assert(idx < static_cast<size_t>(MemTag::COUNT));
    g_tagStats[idx].recordAlloc(static_cast<int64_t>(size));

    return ptr;
}

void SystemAllocator::free(void* ptr, size_t size, MemTag tag) {
    if (!ptr) return;

    const auto idx = static_cast<size_t>(tag);
    assert(idx < static_cast<size_t>(MemTag::COUNT));
    g_tagStats[idx].recordFree(static_cast<int64_t>(size));

    alignedFree(ptr);
}

MemTagStats* SystemAllocator::tagStats() noexcept {
    return g_tagStats;
}

} // namespace engine::core::memory
