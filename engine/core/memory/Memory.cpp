#include "engine/core/memory/Memory.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace Memory {

namespace {

std::array<TagStats, kMemTagCount> s_tagStats{};
std::unique_ptr<FrameArena[]> s_frameArenas;
int s_framesInFlight = 0;

constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

void UpdatePeak(TagStats& stats, std::size_t current) {
    std::size_t peak = stats.peakBytes.load(std::memory_order_relaxed);
    while (current > peak && !stats.peakBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
    }
}

} // namespace

void* SystemAllocator::Alloc(std::size_t size, std::size_t alignment, MemTag tag) {
    if (size == 0) {
        return nullptr;
    }

    alignment = std::max(alignment, alignof(AllocationHeader));
    assert((alignment & (alignment - 1)) == 0);

    std::size_t total = size + alignment + sizeof(AllocationHeader);
    void* raw = std::malloc(total);
    if (!raw) {
        return nullptr;
    }

    std::uintptr_t rawAddress = reinterpret_cast<std::uintptr_t>(raw) + sizeof(AllocationHeader);
    std::uintptr_t alignedAddress = AlignUp(rawAddress, alignment);
    auto* header = reinterpret_cast<AllocationHeader*>(alignedAddress - sizeof(AllocationHeader));
    header->rawPtr = raw;
    header->size = size;

    TagStats& stats = s_tagStats[ToIndex(tag)];
    std::size_t current = stats.currentBytes.fetch_add(size, std::memory_order_relaxed) + size;
    stats.allocCount.fetch_add(1, std::memory_order_relaxed);
    UpdatePeak(stats, current);

    return reinterpret_cast<void*>(alignedAddress);
}

void SystemAllocator::Free(void* ptr, MemTag tag) {
    if (!ptr) {
        return;
    }

    auto* header = reinterpret_cast<AllocationHeader*>(reinterpret_cast<std::uintptr_t>(ptr) - sizeof(AllocationHeader));
    void* raw = header->rawPtr;
    std::size_t size = header->size;

    TagStats& stats = s_tagStats[ToIndex(tag)];
    stats.currentBytes.fetch_sub(size, std::memory_order_relaxed);
    stats.freeCount.fetch_add(1, std::memory_order_relaxed);

    std::free(raw);
}

void* Alloc(std::size_t size, std::size_t alignment, MemTag tag) {
    return SystemAllocator::Alloc(size, alignment, tag);
}

void Free(void* ptr, MemTag tag) {
    SystemAllocator::Free(ptr, tag);
}

LinearArena::LinearArena(std::size_t size, MemTag tag) {
    Initialize(size, tag);
}

void LinearArena::Initialize(std::size_t size, MemTag tag) {
    Destroy();
    if (size == 0) {
        return;
    }
    m_tag = tag;
    m_base = static_cast<std::byte*>(SystemAllocator::Alloc(size, alignof(std::max_align_t), tag));
    m_size = size;
    m_offset = 0;
}

void LinearArena::Destroy() {
    if (m_base) {
        SystemAllocator::Free(m_base, m_tag);
    }
    m_base = nullptr;
    m_size = 0;
    m_offset = 0;
}

void* LinearArena::Allocate(std::size_t size, std::size_t alignment) {
    assert(m_base != nullptr);
    alignment = std::max(alignment, alignof(std::max_align_t));
    assert((alignment & (alignment - 1)) == 0);

    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(m_base) + m_offset;
    std::uintptr_t aligned = AlignUp(current, alignment);
    std::size_t newOffset = static_cast<std::size_t>(aligned - reinterpret_cast<std::uintptr_t>(m_base)) + size;
    assert(newOffset <= m_size);

    m_offset = newOffset;
    return reinterpret_cast<void*>(aligned);
}

void LinearArena::Reset() {
    m_offset = 0;
}

void InitializeFrameArenas(std::size_t perFrameSize, int framesInFlight, MemTag tag) {
    ShutdownFrameArenas();
    if (framesInFlight <= 0) {
        return;
    }

    s_frameArenas = std::make_unique<FrameArena[]>(static_cast<std::size_t>(framesInFlight));
    s_framesInFlight = framesInFlight;
    for (int i = 0; i < framesInFlight; ++i) {
        s_frameArenas[static_cast<std::size_t>(i)].Initialize(perFrameSize, tag);
    }
}

void ShutdownFrameArenas() {
    if (!s_frameArenas) {
        s_framesInFlight = 0;
        return;
    }

    for (int i = 0; i < s_framesInFlight; ++i) {
        s_frameArenas[static_cast<std::size_t>(i)].Destroy();
    }
    s_frameArenas.reset();
    s_framesInFlight = 0;
}

void BeginFrame(int frameIndex) {
    assert(frameIndex >= 0);
    assert(frameIndex < s_framesInFlight);
    s_frameArenas[static_cast<std::size_t>(frameIndex)].Reset();
}

FrameArena& GetFrameArena(int frameIndex) {
    assert(frameIndex >= 0);
    assert(frameIndex < s_framesInFlight);
    return s_frameArenas[static_cast<std::size_t>(frameIndex)];
}

int FramesInFlight() {
    return s_framesInFlight;
}

void DumpStats() {
    std::printf("Memory stats by tag:\n");
    std::printf("%-8s %-12s %-12s %-12s %-12s\n", "Tag", "Current", "Peak", "Allocs", "Frees");
    for (std::size_t i = 0; i < kMemTagCount; ++i) {
        const TagStats& stats = s_tagStats[i];
        std::printf("%-8s %-12zu %-12zu %-12zu %-12zu\n",
                    kMemTagNames[i],
                    stats.currentBytes.load(std::memory_order_relaxed),
                    stats.peakBytes.load(std::memory_order_relaxed),
                    stats.allocCount.load(std::memory_order_relaxed),
                    stats.freeCount.load(std::memory_order_relaxed));
    }
}

} // namespace Memory
