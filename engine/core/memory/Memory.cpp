/**
 * @file Memory.cpp
 * @brief SystemAllocator, LinearArena, FrameArena, Memory::DumpStats implementation.
 */

#include "engine/core/memory/Memory.h"
#include "engine/core/Log.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace engine::core {

// --- MemTag names (for DumpStats) ---
static const char* s_memTagNames[] = {
    "Core", "Render", "Assets", "World", "Net", "UI", "Tools", "Temp"
};

const char* MemTagName(MemTag tag) {
    auto i = static_cast<size_t>(tag);
    if (i >= static_cast<size_t>(MemTag::_Count)) return "?";
    return s_memTagNames[i];
}

// --- SystemAllocator tracking (atomic for thread safety) ---
namespace {
struct TagState {
    std::atomic<size_t> currentBytes{0};
    std::atomic<size_t> peakBytes{0};
    std::atomic<uint64_t> allocCount{0};
    std::atomic<uint64_t> freeCount{0};
};
TagState s_tagStates[static_cast<size_t>(MemTag::_Count)];
} // namespace

namespace {
constexpr size_t kAllocHeaderSize = sizeof(size_t);
}

void* SystemAllocator::Alloc(size_t size, size_t align, MemTag tag) {
    if (size == 0) return nullptr;
    size_t a = (align > 0 && (align & (align - 1)) == 0) ? align : 1;
    if (a < kAllocHeaderSize) a = kAllocHeaderSize;
    size_t total = size + kAllocHeaderSize;
    void* raw = nullptr;
#ifdef _WIN32
    raw = _aligned_malloc(total, a);
#else
    if (posix_memalign(&raw, a, total) != 0)
        raw = nullptr;
#endif
    if (!raw) return nullptr;
    *static_cast<size_t*>(raw) = size;
    void* ptr = static_cast<char*>(raw) + kAllocHeaderSize;
    size_t idx = static_cast<size_t>(tag);
    if (idx < static_cast<size_t>(MemTag::_Count)) {
        s_tagStates[idx].currentBytes.fetch_add(size, std::memory_order_relaxed);
        s_tagStates[idx].allocCount.fetch_add(1, std::memory_order_relaxed);
        size_t cur = s_tagStates[idx].currentBytes.load(std::memory_order_relaxed);
        size_t peak = s_tagStates[idx].peakBytes.load(std::memory_order_relaxed);
        while (cur > peak && !s_tagStates[idx].peakBytes.compare_exchange_weak(peak, cur, std::memory_order_relaxed))
            {}
    }
    return ptr;
}

void SystemAllocator::Free(void* ptr, MemTag tag) {
    if (!ptr) return;
    void* raw = static_cast<char*>(ptr) - kAllocHeaderSize;
    size_t size = *static_cast<size_t*>(raw);
    size_t idx = static_cast<size_t>(tag);
    if (idx < static_cast<size_t>(MemTag::_Count)) {
        s_tagStates[idx].currentBytes.fetch_sub(size, std::memory_order_relaxed);
        s_tagStates[idx].freeCount.fetch_add(1, std::memory_order_relaxed);
    }
#ifdef _WIN32
    _aligned_free(raw);
#else
    free(raw);
#endif
}

TagStats SystemAllocator::GetTagStats(MemTag tag) {
    size_t idx = static_cast<size_t>(tag);
    TagStats s;
    if (idx < static_cast<size_t>(MemTag::_Count)) {
        s.currentBytes = s_tagStates[idx].currentBytes.load(std::memory_order_relaxed);
        s.peakBytes = s_tagStates[idx].peakBytes.load(std::memory_order_relaxed);
        s.allocCount = s_tagStates[idx].allocCount.load(std::memory_order_relaxed);
        s.freeCount = s_tagStates[idx].freeCount.load(std::memory_order_relaxed);
    }
    return s;
}

// --- LinearArena ---
void LinearArena::Init(void* buffer, size_t capacity) {
    m_buffer = static_cast<uint8_t*>(buffer);
    m_capacity = capacity;
    m_used = 0;
}

void* LinearArena::Alloc(size_t size, size_t align) {
    if (size == 0) return nullptr;
    size_t a = (align > 0 && (align & (align - 1)) == 0) ? align : 1;
    size_t pad = (a - (reinterpret_cast<uintptr_t>(m_buffer + m_used) % a)) % a;
    size_t need = m_used + pad + size;
    assert(need <= m_capacity && "LinearArena overflow");
    if (need > m_capacity) return nullptr;
    m_used += pad;
    void* ptr = m_buffer + m_used;
    m_used += size;
    return ptr;
}

void LinearArena::Reset() {
    m_used = 0;
}

// --- Frame arenas + Memory:: ---
namespace {
LinearArena s_frameArenas[kMaxFramesInFlight];
void* s_frameArenaBuffers[kMaxFramesInFlight] = {};
int s_framesInFlight = 0;
} // namespace

void Memory::Init(int framesInFlight, size_t capacityPerFrame) {
    assert(framesInFlight > 0 && framesInFlight <= kMaxFramesInFlight);
    Shutdown();
    s_framesInFlight = framesInFlight;
    for (int i = 0; i < framesInFlight; ++i) {
        void* buf = SystemAllocator::Alloc(capacityPerFrame, 16, MemTag::Temp);
        assert(buf && "Frame arena alloc failed");
        s_frameArenaBuffers[i] = buf;
        s_frameArenas[i].Init(buf, capacityPerFrame);
    }
}

void Memory::Shutdown() {
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (s_frameArenaBuffers[i]) {
            SystemAllocator::Free(s_frameArenaBuffers[i], MemTag::Temp);
            s_frameArenaBuffers[i] = nullptr;
        }
        s_frameArenas[i].Init(nullptr, 0);
    }
    s_framesInFlight = 0;
}

void Memory::BeginFrame(uint64_t frameIndex) {
    int slot = static_cast<int>(frameIndex % static_cast<uint64_t>(s_framesInFlight));
    s_frameArenas[slot].Reset();
}

LinearArena* Memory::GetFrameArena(uint64_t frameIndex) {
    int slot = static_cast<int>(frameIndex % static_cast<uint64_t>(s_framesInFlight));
    return &s_frameArenas[slot];
}

void Memory::DumpStats() {
    for (size_t i = 0; i < static_cast<size_t>(MemTag::_Count); ++i) {
        TagStats s = SystemAllocator::GetTagStats(static_cast<MemTag>(i));
        if (s.allocCount != 0 || s.currentBytes != 0 || s.peakBytes != 0) {
            LOG_INFO(Core, "Mem[%s] current=%zu peak=%zu allocs=%llu frees=%llu",
                MemTagName(static_cast<MemTag>(i)),
                s.currentBytes, s.peakBytes,
                static_cast<unsigned long long>(s.allocCount),
                static_cast<unsigned long long>(s.freeCount));
        }
    }
}

} // namespace engine::core
