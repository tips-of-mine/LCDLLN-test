/**
 * @file MemoryTags.h
 * @brief Memory tags per system for allocation tracking and DumpStats.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::core {

/** Tags for allocation tracking (current/peak/count per tag). */
enum class MemTag : uint8_t {
    Core,
    Render,
    Assets,
    World,
    Net,
    UI,
    Tools,
    Temp,
    _Count
};

/** Per-tag stats: current bytes, peak bytes, alloc/free count. */
struct TagStats {
    size_t currentBytes = 0;
    size_t peakBytes = 0;
    uint64_t allocCount = 0;
    uint64_t freeCount = 0;
};

/** Human-readable name for a tag (for DumpStats). */
const char* MemTagName(MemTag tag);

} // namespace engine::core
