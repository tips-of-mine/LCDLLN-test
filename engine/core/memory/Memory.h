/**
 * @file Memory.h
 * @brief SystemAllocator (aligned alloc/free + tracking), LinearArena, FrameArena, DumpStats.
 */

#pragma once

#include "engine/core/memory/MemoryTags.h"
#include <cstddef>
#include <cstdint>

namespace engine::core {

/**
 * System allocator: aligned alloc/free with per-tag tracking (current/peak/count).
 * Thread-safe via atomics.
 */
class SystemAllocator {
public:
    /** Allocate aligned memory; tag used for tracking. Returns nullptr on failure. */
    static void* Alloc(size_t size, size_t align, MemTag tag);

    /** Free memory previously allocated with Alloc; tag must match. */
    static void Free(void* ptr, MemTag tag);

    /** Return stats for a tag (snapshot). */
    static TagStats GetTagStats(MemTag tag);
};

/**
 * Linear (bump) arena: alloc only, no per-allocation free; Reset() in O(1).
 * Overflow is asserted.
 */
class LinearArena {
public:
    LinearArena() = default;
    /** Initialize with backing buffer (arena does not own it). Capacity in bytes. */
    void Init(void* buffer, size_t capacity);
    /** Allocate from arena; alignment must be power of two. Returns nullptr if overflow. */
    void* Alloc(size_t size, size_t align);
    /** Reset bump pointer to start (O(1)); no individual frees. */
    void Reset();
    /** Total capacity in bytes. */
    size_t Capacity() const { return m_capacity; }
    /** Currently used bytes. */
    size_t Used() const { return m_used; }

private:
    uint8_t* m_buffer = nullptr;
    size_t m_capacity = 0;
    size_t m_used = 0;
};

/** Default frames-in-flight for frame arenas. */
constexpr int kDefaultFramesInFlight = 2;

/** Max frames-in-flight supported. */
constexpr int kMaxFramesInFlight = 4;

/** Default capacity per frame arena (bytes). */
constexpr size_t kDefaultFrameArenaCapacity = 1024 * 1024; // 1 MiB

/**
 * Memory subsystem: frame arenas + Init/BeginFrame/DumpStats.
 */
namespace Memory {

/** Initialize frame arenas (call once at startup). capacityPerFrame optional. */
void Init(int framesInFlight = kDefaultFramesInFlight, size_t capacityPerFrame = kDefaultFrameArenaCapacity);

/** Shutdown and release frame arena buffers. */
void Shutdown();

/** Reset the frame arena for the given frame index (call at start of frame). */
void BeginFrame(uint64_t frameIndex);

/** Get the linear arena for the current frame slot (for the given frame index). */
LinearArena* GetFrameArena(uint64_t frameIndex);

/** Dump stats per tag (e.g. to log). */
void DumpStats();

} // namespace Memory

} // namespace engine::core
