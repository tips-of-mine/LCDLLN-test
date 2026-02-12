#include "engine/core/memory/Memory.h"

#include "engine/core/Log.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>

namespace engine::core::memory {
namespace {

struct AllocationHeader {
    void* original = nullptr;
    std::size_t size = 0;
    MemTag tag = MemTag::Core;
    std::uint32_t guard = 0xC0DEC0DEu;
};

constexpr std::size_t ToIndex(const MemTag tag) {
    return static_cast<std::size_t>(tag);
}

std::byte* AlignPointer(std::byte* ptr, std::size_t alignment) {
    const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
    const auto aligned = (raw + (alignment - 1)) & ~(alignment - 1);
    return reinterpret_cast<std::byte*>(aligned);
}

} // namespace

std::array<MemTagStats, kMemTagCount> SystemAllocator::s_stats{};

void* SystemAllocator::Alloc(const std::size_t size, const std::size_t alignment, const MemTag tag) {
    assert(size > 0);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0 && "alignment must be a power of two");

    const std::size_t headerSize = sizeof(AllocationHeader);
    const std::size_t padding = alignment - 1;
    const std::size_t total = size + headerSize + padding;

    auto* original = static_cast<std::byte*>(std::malloc(total));
    if (!original) {
        throw std::bad_alloc();
    }

    std::byte* aligned = AlignPointer(original + headerSize, alignment);
    auto* header = reinterpret_cast<AllocationHeader*>(aligned - headerSize);
    header->original = original;
    header->size = size;
    header->tag = tag;
    header->guard = 0xC0DEC0DEu;

    TrackAlloc(tag, size);
    return aligned;
}

void SystemAllocator::Free(void* ptr, const MemTag tag) {
    if (!ptr) {
        return;
    }

    const std::size_t headerSize = sizeof(AllocationHeader);
    auto* aligned = static_cast<std::byte*>(ptr);
    auto* header = reinterpret_cast<AllocationHeader*>(aligned - headerSize);

    assert(header->guard == 0xC0DEC0DEu && "Memory header corrupted");
    assert(header->tag == tag && "Tag mismatch in SystemAllocator::Free");

    TrackFree(header->tag, header->size);
    std::free(header->original);
}

void SystemAllocator::DumpStats() {
    LOG_INFO(Core, "--- Memory stats by tag ---");
    for (std::size_t i = 0; i < kMemTagCount; ++i) {
        const auto& stats = s_stats[i];
        LOG_INFO(Core,
                 kMemTagNames[i],
                 " current=", stats.currentBytes.load(),
                 " peak=", stats.peakBytes.load(),
                 " allocs=", stats.allocCount.load(),
                 " frees=", stats.freeCount.load());
    }
}

void SystemAllocator::TrackAlloc(const MemTag tag, const std::size_t size) {
    auto& stats = s_stats[ToIndex(tag)];
    const std::size_t current = stats.currentBytes.fetch_add(size) + size;
    stats.allocCount.fetch_add(1);

    std::size_t peak = stats.peakBytes.load();
    while (current > peak && !stats.peakBytes.compare_exchange_weak(peak, current)) {
    }
}

void SystemAllocator::TrackFree(const MemTag tag, const std::size_t size) {
    auto& stats = s_stats[ToIndex(tag)];
    stats.currentBytes.fetch_sub(size);
    stats.freeCount.fetch_add(1);
}

LinearArena::LinearArena(const std::size_t capacity, const MemTag backingTag) {
    Initialize(capacity, backingTag);
}

LinearArena::~LinearArena() {
    Shutdown();
}

LinearArena::LinearArena(LinearArena&& other) noexcept {
    m_buffer = std::exchange(other.m_buffer, nullptr);
    m_capacity = std::exchange(other.m_capacity, 0);
    m_offset = std::exchange(other.m_offset, 0);
    m_backingTag = other.m_backingTag;
}

LinearArena& LinearArena::operator=(LinearArena&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Shutdown();

    m_buffer = std::exchange(other.m_buffer, nullptr);
    m_capacity = std::exchange(other.m_capacity, 0);
    m_offset = std::exchange(other.m_offset, 0);
    m_backingTag = other.m_backingTag;
    return *this;
}

void LinearArena::Initialize(const std::size_t capacity, const MemTag backingTag) {
    Shutdown();
    assert(capacity > 0);

    m_buffer = static_cast<std::byte*>(SystemAllocator::Alloc(capacity, alignof(std::max_align_t), backingTag));
    m_capacity = capacity;
    m_offset = 0;
    m_backingTag = backingTag;
}

void LinearArena::Shutdown() {
    if (!m_buffer) {
        return;
    }

    SystemAllocator::Free(m_buffer, m_backingTag);
    m_buffer = nullptr;
    m_capacity = 0;
    m_offset = 0;
}

void* LinearArena::Alloc(const std::size_t size, const std::size_t alignment) {
    assert(m_buffer && "LinearArena is not initialized");
    assert(size > 0);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0 && "alignment must be a power of two");

    const std::size_t base = reinterpret_cast<std::uintptr_t>(m_buffer) + m_offset;
    const std::size_t aligned = (base + (alignment - 1)) & ~(alignment - 1);
    const std::size_t next = (aligned - reinterpret_cast<std::uintptr_t>(m_buffer)) + size;

    assert(next <= m_capacity && "LinearArena overflow");
    if (next > m_capacity) {
        return nullptr;
    }

    m_offset = next;
    return reinterpret_cast<void*>(aligned);
}

void LinearArena::Reset() {
    m_offset = 0;
}

std::size_t LinearArena::Capacity() const {
    return m_capacity;
}

std::size_t LinearArena::Used() const {
    return m_offset;
}

std::array<LinearArena, Memory::kFramesInFlight> Memory::s_frameArenas{};
std::size_t Memory::s_currentFrameArena = 0;
bool Memory::s_initialized = false;

void Memory::InitializeFrameArenas(const std::size_t bytesPerFrameArena) {
    if (s_initialized) {
        return;
    }

    for (auto& arena : s_frameArenas) {
        arena.Initialize(bytesPerFrameArena, MemTag::Temp);
    }
    s_currentFrameArena = 0;
    s_initialized = true;
}

void Memory::ShutdownFrameArenas() {
    if (!s_initialized) {
        return;
    }

    for (auto& arena : s_frameArenas) {
        arena.Shutdown();
    }
    s_initialized = false;
    s_currentFrameArena = 0;
}

void Memory::BeginFrame() {
    if (!s_initialized) {
        return;
    }

    s_currentFrameArena = (s_currentFrameArena + 1) % kFramesInFlight;
    s_frameArenas[s_currentFrameArena].Reset();
}

LinearArena& Memory::CurrentFrameArena() {
    assert(s_initialized && "Frame arenas not initialized");
    return s_frameArenas[s_currentFrameArena];
}

void Memory::DumpStats() {
    SystemAllocator::DumpStats();
}

} // namespace engine::core::memory
