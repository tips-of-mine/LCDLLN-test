// engine/core/memory/Memory.cpp
// M00.2 — Memory subsystem implementation.

#include "Memory.h"
#include "SystemAllocator.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

namespace {

struct FrameArenaState {
    LinearArena arena;
    void*       backingBuffer = nullptr;
    size_t      capacity      = 0;
};

static uint32_t          g_framesInFlight = 0;
static FrameArenaState   g_frameArenas[kMaxFramesInFlight];
static bool              g_initialised = false;

/// Human-readable names aligned with MemTag enum order.
static const char* kTagNames[static_cast<size_t>(MemTag::COUNT)] = {
    "Core  ",
    "Render",
    "Assets",
    "World ",
    "Net   ",
    "UI    ",
    "Tools ",
    "Temp  ",
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Memory::Init / Shutdown
// ---------------------------------------------------------------------------

void Memory::Init(uint32_t framesInFlight, size_t arenaCapacity) {
    assert(!g_initialised && "Memory::Init called twice");
    assert(framesInFlight > 0 && framesInFlight <= kMaxFramesInFlight);
    assert(arenaCapacity > 0);

    g_framesInFlight = framesInFlight;

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        // Allocate backing memory for each frame arena via SystemAllocator.
        void* buf = SystemAllocator::alloc(arenaCapacity, 64, MemTag::Core);
        g_frameArenas[i].backingBuffer = buf;
        g_frameArenas[i].capacity      = arenaCapacity;
        g_frameArenas[i].arena.init(buf, arenaCapacity, MemTag::Temp);
    }

    g_initialised = true;
}

void Memory::Shutdown() {
    assert(g_initialised && "Memory::Shutdown called before Init");

    for (uint32_t i = 0; i < g_framesInFlight; ++i) {
        SystemAllocator::free(
            g_frameArenas[i].backingBuffer,
            g_frameArenas[i].capacity,
            MemTag::Core);
        g_frameArenas[i].backingBuffer = nullptr;
    }

    g_framesInFlight = 0;
    g_initialised    = false;
}

// ---------------------------------------------------------------------------
// Memory::Alloc / Free
// ---------------------------------------------------------------------------

void* Memory::Alloc(size_t size, size_t alignment, MemTag tag) {
    return SystemAllocator::alloc(size, alignment, tag);
}

void Memory::Free(void* ptr, size_t size, MemTag tag) {
    SystemAllocator::free(ptr, size, tag);
}

// ---------------------------------------------------------------------------
// Memory::BeginFrame
// ---------------------------------------------------------------------------

void Memory::BeginFrame(uint32_t frameIndex) {
    assert(g_initialised);
    assert(frameIndex < g_framesInFlight && "frameIndex out of range");
    // Reset the frame arena: O(1) reclamation.
    g_frameArenas[frameIndex].arena.reset();
}

LinearArena& Memory::GetFrameArena(uint32_t frameIndex) {
    assert(g_initialised);
    assert(frameIndex < g_framesInFlight && "frameIndex out of range");
    return g_frameArenas[frameIndex].arena;
}

// ---------------------------------------------------------------------------
// Memory::DumpStats
// ---------------------------------------------------------------------------

void Memory::DumpStats() {
    const MemTagStats* stats = SystemAllocator::tagStats();

    // Table header
    std::printf(
        "\n"
        "+--------+--------------------+--------------------+------------+------------+\n"
        "| Tag    |   Current (bytes)  |     Peak (bytes)   |   Allocs   |   Frees    |\n"
        "+--------+--------------------+--------------------+------------+------------+\n");

    for (size_t i = 0; i < static_cast<size_t>(MemTag::COUNT); ++i) {
        const int64_t  cur    = stats[i].currentBytes.load(std::memory_order_relaxed);
        const int64_t  peak   = stats[i].peakBytes.load(std::memory_order_relaxed);
        const uint64_t allocs = stats[i].allocCount.load(std::memory_order_relaxed);
        const uint64_t frees  = stats[i].freeCount.load(std::memory_order_relaxed);

        std::printf("| %s | %18lld | %18lld | %10llu | %10llu |\n",
            kTagNames[i],
            static_cast<long long>(cur),
            static_cast<long long>(peak),
            static_cast<unsigned long long>(allocs),
            static_cast<unsigned long long>(frees));
    }

    std::printf(
        "+--------+--------------------+--------------------+------------+------------+\n\n");
}

} // namespace engine::core::memory
