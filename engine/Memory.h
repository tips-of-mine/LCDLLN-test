#pragma once
// engine/core/memory/Memory.h
// M00.2 — Memory subsystem façade.
//
// Provides:
//   - Generic alloc/free forwarded to SystemAllocator.
//   - FrameArenas (one per frame-in-flight), reset at BeginFrame().
//   - DumpStats(): human-readable table of per-tag statistics.
//
// Typical integration in the engine loop:
//   Memory::Init(framesInFlight, arenaCapacityPerFrame);
//   loop {
//       Memory::BeginFrame(frameIndex);   // resets this frame's arena
//       ... tick/render ...
//   }
//   Memory::Shutdown();

#include "MemoryTags.h"
#include "LinearArena.h"

#include <cstddef>
#include <cstdint>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

/// Maximum number of frames-in-flight supported.
static constexpr uint32_t kMaxFramesInFlight = 3;

// ---------------------------------------------------------------------------
// Memory façade
// ---------------------------------------------------------------------------

class Memory {
public:
    /// @brief Initialise the memory subsystem.
    /// @param framesInFlight  Number of frame arenas to create (1..kMaxFramesInFlight).
    /// @param arenaCapacity   Bytes reserved per frame arena (backing heap alloc).
    static void Init(uint32_t framesInFlight, size_t arenaCapacity);

    /// @brief Tear down frame arenas and release their backing buffers.
    static void Shutdown();

    // ---- Generic alloc / free --------------------------------------------

    /// @brief Allocate @p size bytes aligned to @p alignment, tagged @p tag.
    /// Forwards to SystemAllocator; updates per-tag stats.
    [[nodiscard]] static void* Alloc(size_t size, size_t alignment, MemTag tag);

    /// @brief Release memory previously returned by Alloc().
    /// @param size  Same byte-count supplied to Alloc().
    /// @param tag   Same tag supplied to Alloc().
    static void Free(void* ptr, size_t size, MemTag tag);

    // ---- Frame arenas ----------------------------------------------------

    /// @brief Call at the start of each frame before any frame-arena usage.
    /// Resets the arena corresponding to @p frameIndex.
    /// @param frameIndex  Index of the current in-flight frame (0-based).
    static void BeginFrame(uint32_t frameIndex);

    /// @brief Returns the arena for the currently active frame.
    /// Valid only after BeginFrame() for the current @p frameIndex.
    static LinearArena& GetFrameArena(uint32_t frameIndex);

    // ---- Stats -----------------------------------------------------------

    /// @brief Print per-tag allocation statistics to stdout.
    /// Columns: tag name | current bytes | peak bytes | allocs | frees.
    static void DumpStats();
};

} // namespace engine::core::memory
