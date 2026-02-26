#pragma once
// engine/core/memory/SystemAllocator.h
// M00.2 — SystemAllocator: aligned heap alloc/free + per-tag tracking.

#include "MemoryTags.h"
#include <cstddef>

namespace engine::core::memory {

/// @brief Wrapper around the OS heap providing aligned allocation
///        with per-MemTag tracking (current bytes, peak, counts).
///
/// Usage:
///   void* ptr = SystemAllocator::alloc(1024, 16, MemTag::Render);
///   SystemAllocator::free(ptr, 1024, MemTag::Render);
///
/// Notes:
/// - alloc() asserts on nullptr (OOM is treated as fatal).
/// - free() requires the original byte-count to update stats; the
///   caller is responsible for passing the same size used in alloc().
/// - Thread-safe: tag statistics use std::atomic operations.
class SystemAllocator {
public:
    /// Allocate @p size bytes aligned to @p alignment, tagged @p tag.
    /// @param size      Number of bytes to allocate (must be > 0).
    /// @param alignment Alignment in bytes (must be a power-of-two ≥ 1).
    /// @param tag       Subsystem tag for tracking.
    /// @return          Non-null pointer to aligned memory.
    [[nodiscard]] static void* alloc(size_t size, size_t alignment, MemTag tag);

    /// Release memory previously returned by alloc().
    /// @param ptr       Pointer returned by alloc() (may be nullptr, no-op).
    /// @param size      Same byte-count passed to the matching alloc().
    /// @param tag       Same tag passed to the matching alloc().
    static void free(void* ptr, size_t size, MemTag tag);

    /// Returns the global stats array (one entry per MemTag).
    /// Lifetime: static storage duration.
    static MemTagStats* tagStats() noexcept;
};

} // namespace engine::core::memory
