// engine/core/memory/tests/memory_smoke_test.cpp
// M00.2 — Smoke test: validates LinearArena + SystemAllocator + DumpStats.
//
// Criteria from ticket DoD:
//   - 10k allocs arena → reset → 10k allocs without crash/leak.
//   - DumpStats reflects allocations per tag (Render vs World).
//   - No individual free required in LinearArena.

#include "../Memory.h"
#include "../SystemAllocator.h"
#include "../LinearArena.h"

#include <cstdio>
#include <cstdint>
#include <cassert>

using namespace engine::core::memory;

int main() {
    std::printf("=== M00.2 Memory smoke test ===\n\n");

    // ----------------------------------------------------------------
    // 1) SystemAllocator: Render vs World tracking
    // ----------------------------------------------------------------
    constexpr size_t kBlockSize = 64;
    constexpr size_t kAlign     = 16;

    void* renderPtr = Memory::Alloc(kBlockSize, kAlign, MemTag::Render);
    void* worldPtr  = Memory::Alloc(kBlockSize, kAlign, MemTag::World);

    std::printf("[SystemAllocator] Allocated %zu bytes tagged Render\n", kBlockSize);
    std::printf("[SystemAllocator] Allocated %zu bytes tagged World\n", kBlockSize);

    // ----------------------------------------------------------------
    // 2) FrameArena: Init + 10k allocs + reset + 10k allocs
    // ----------------------------------------------------------------
    constexpr uint32_t kFrames    = 2;
    constexpr size_t   kArenaSize = 4 * 1024 * 1024; // 4 MiB per frame

    Memory::Init(kFrames, kArenaSize);

    std::printf("\n[FrameArena] Init: %u frames, %zu bytes each\n", kFrames, kArenaSize);

    // First pass: 10 000 allocs of 8 bytes in frame 0
    constexpr uint32_t kAllocCount = 10000;
    constexpr size_t   kAllocSize  = 8;

    Memory::BeginFrame(0); // reset frame 0 arena

    for (uint32_t i = 0; i < kAllocCount; ++i) {
        void* p = Memory::GetFrameArena(0).alloc(kAllocSize, 8);
        (void)p; // no individual free – correct for LinearArena
    }

    std::printf("[FrameArena] Pass 1: %u allocs OK  (used=%zu)\n",
        kAllocCount, Memory::GetFrameArena(0).used());

    // Reset O(1)
    Memory::BeginFrame(0);
    assert(Memory::GetFrameArena(0).used() == 0 && "Arena not reset!");
    std::printf("[FrameArena] Reset OK (used=%zu)\n", Memory::GetFrameArena(0).used());

    // Second pass: 10 000 allocs again after reset
    for (uint32_t i = 0; i < kAllocCount; ++i) {
        void* p = Memory::GetFrameArena(0).alloc(kAllocSize, 8);
        (void)p;
    }

    std::printf("[FrameArena] Pass 2: %u allocs OK  (used=%zu)\n",
        kAllocCount, Memory::GetFrameArena(0).used());

    // ----------------------------------------------------------------
    // 3) Free heap allocations to verify tracker
    // ----------------------------------------------------------------
    Memory::Free(renderPtr, kBlockSize, MemTag::Render);
    Memory::Free(worldPtr,  kBlockSize, MemTag::World);

    // ----------------------------------------------------------------
    // 4) DumpStats – must show Render & World activity
    // ----------------------------------------------------------------
    std::printf("\n[DumpStats] Per-tag statistics:\n");
    Memory::DumpStats();

    // ----------------------------------------------------------------
    // 5) Shutdown
    // ----------------------------------------------------------------
    Memory::Shutdown();

    std::printf("=== Smoke test PASSED ===\n");
    return 0;
}
