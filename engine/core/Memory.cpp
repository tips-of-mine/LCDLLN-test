/**
 * @file Memory.cpp
 * @brief Memory API implementation (Alloc, Free, DumpStats).
 */

#include "Memory.h"

#include <array>
#include <cstdio>
#include <cstdint>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Tag name table (used by DumpStats)
// ---------------------------------------------------------------------------

namespace {

/// Human-readable name for each MemTag (indexed by cast to uint8_t).
constexpr std::array<const char*, static_cast<std::size_t>(MemTag::Count)>
    kTagNames = {
        "Core   ",   // 0
        "Render ",   // 1
        "Assets ",   // 2
        "World  ",   // 3
        "Net    ",   // 4
        "UI     ",   // 5
        "Tools  ",   // 6
        "Temp   ",   // 7
    };

} // namespace

// ---------------------------------------------------------------------------
// Memory::Alloc
// ---------------------------------------------------------------------------

void* Memory::Alloc(std::size_t size,
                    std::size_t align,
                    MemTag      tag) noexcept {
    return SystemAllocator::Alloc(size, align, tag);
}

// ---------------------------------------------------------------------------
// Memory::Free
// ---------------------------------------------------------------------------

void Memory::Free(void* ptr, std::size_t size, MemTag tag) noexcept {
    SystemAllocator::Free(ptr, size, tag);
}

// ---------------------------------------------------------------------------
// Memory::DumpStats
// ---------------------------------------------------------------------------

void Memory::DumpStats() {
    std::printf("\n");
    std::printf("=======================================================\n");
    std::printf("  MEMORY STATS  (by MemTag)\n");
    std::printf("=======================================================\n");
    std::printf("  %-10s  %12s  %12s  %10s  %10s\n",
                "Tag", "Current", "Peak", "Allocs", "Frees");
    std::printf("  %-10s  %12s  %12s  %10s  %10s\n",
                "----------", "------------", "------------",
                "----------", "----------");

    for (std::size_t i = 0; i < static_cast<std::size_t>(MemTag::Count); ++i) {
        const MemTag   tag  = static_cast<MemTag>(i);
        const TagStats stat = SystemAllocator::GetStats(tag);

        std::printf("  %-10s  %9zu B   %9zu B   %10llu  %10llu\n",
                    kTagNames[i],
                    stat.currentBytes,
                    stat.peakBytes,
                    static_cast<unsigned long long>(stat.allocCount),
                    static_cast<unsigned long long>(stat.freeCount));
    }

    std::printf("=======================================================\n\n");
}

} // namespace engine::core::memory
