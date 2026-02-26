#pragma once
// engine/core/memory/LinearArena.h
// M00.2 — LinearArena: bump allocator backed by a contiguous block.
//
// Allocation strategy:
//   - Incrementally advance an offset pointer.
//   - No individual free; call reset() to reclaim the entire arena in O(1).
//
// Thread-safety: NOT thread-safe by design (typically per-thread or per-frame).

#include "MemoryTags.h"
#include <cstddef>
#include <cstdint>

namespace engine::core::memory {

/// @brief Bump (linear) allocator over a fixed backing buffer.
///
/// Lifetime of backing memory is NOT managed by this class;
/// the caller provides a pre-allocated buffer or calls init().
class LinearArena {
public:
    LinearArena() = default;

    /// @brief Initialise from a raw backing buffer.
    /// @param buffer   Pointer to backing memory (must remain valid for lifetime).
    /// @param capacity Total usable bytes in @p buffer.
    /// @param tag      MemTag used to attribute sub-allocations in DumpStats.
    void init(void* buffer, size_t capacity, MemTag tag) noexcept;

    /// @brief Allocate @p size bytes aligned to @p alignment.
    /// @param size      Must be > 0.
    /// @param alignment Must be a power-of-two ≥ 1.
    /// @return Pointer to allocated region, or asserts on overflow.
    [[nodiscard]] void* alloc(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

    /// @brief Reset arena to empty – all previously allocated memory is
    ///        reclaimed in O(1).  Pointers from before reset() are invalid.
    void reset() noexcept;

    // ---- Accessors --------------------------------------------------------

    /// Bytes consumed by live allocations.
    size_t used()     const noexcept { return m_offset; }
    /// Total capacity supplied at init().
    size_t capacity() const noexcept { return m_capacity; }
    /// Bytes still available.
    size_t remaining() const noexcept { return m_capacity - m_offset; }

private:
    uint8_t* m_buffer   = nullptr; ///< Base of backing memory
    size_t   m_capacity = 0;       ///< Size of backing memory
    size_t   m_offset   = 0;       ///< Current allocation cursor
    MemTag   m_tag      = MemTag::Temp;
};

} // namespace engine::core::memory
