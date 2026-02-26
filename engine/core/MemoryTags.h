#pragma once

/**
 * @file MemoryTags.h
 * @brief Memory tagging system: categorises every allocation by engine subsystem.
 *
 * Each MemTag corresponds to one subsystem. Tracking stats (current bytes,
 * peak bytes, alloc count, free count) are maintained per tag by the
 * SystemAllocator.
 */

#include <cstddef>
#include <cstdint>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// MemTag — subsystem identifier
// ---------------------------------------------------------------------------

/// @brief Identifies the engine subsystem responsible for an allocation.
enum class MemTag : uint8_t {
    Core    = 0, ///< Core subsystems (Log, Time, Config)
    Render  = 1, ///< Rendering subsystem
    Assets  = 2, ///< Asset loading and management
    World   = 3, ///< World/scene data
    Net     = 4, ///< Networking
    UI      = 5, ///< User interface
    Tools   = 6, ///< Offline tooling
    Temp    = 7, ///< Short-lived / scratch allocations

    Count        ///< Sentinel: total number of tags (must remain last)
};

// ---------------------------------------------------------------------------
// TagStats — per-tag allocation statistics
// ---------------------------------------------------------------------------

/// @brief Allocation statistics for one MemTag.
struct TagStats {
    std::size_t  currentBytes = 0; ///< Bytes currently live (alloc - free)
    std::size_t  peakBytes    = 0; ///< Highest value ever observed for currentBytes
    uint64_t     allocCount   = 0; ///< Total number of successful allocations
    uint64_t     freeCount    = 0; ///< Total number of frees
};

} // namespace engine::core::memory
