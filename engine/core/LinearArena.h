#pragma once

/**
 * @file LinearArena.h
 * @brief Linear (bump) allocator — fast alloc, O(1) reset, no individual free.
 *
 * A LinearArena owns a contiguous block of memory and services allocations
 * by advancing a cursor (the "bump pointer"). Individual frees are not
 * supported; the entire arena is reclaimed at once via Reset().
 *
 * Overflow is caught with an assert; callers must size the arena adequately.
 *
 * Usage:
 *   LinearArena arena(4 * 1024 * 1024, MemTag::Render); // 4 MiB
 *   void* p = arena.Alloc(1024, alignof(float));
 *   arena.Reset(); // reclaims all memory
 */

#include "MemoryTags.h"

#include <cstddef>

namespace engine::core::memory {

/// @brief Bump allocator backed by a SystemAllocator block.
class LinearArena {
public:
    /**
     * @brief Construct and allocate backing storage.
     * @param capacityBytes Total capacity in bytes (must be > 0).
     * @param tag           MemTag for the backing allocation.
     */
    explicit LinearArena(std::size_t capacityBytes, MemTag tag = MemTag::Temp);

    /// @brief Destructor: releases the backing storage.
    ~LinearArena();

    // Non-copyable, non-movable (owns raw memory).
    LinearArena(const LinearArena&)            = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    LinearArena(LinearArena&&)                 = delete;
    LinearArena& operator=(LinearArena&&)      = delete;

    /**
     * @brief Allocate `size` bytes with `align` alignment.
     *
     * Advances the bump pointer; asserts on overflow.
     *
     * @param size  Bytes to allocate (must be > 0).
     * @param align Alignment in bytes (must be a power of two, >= 1).
     * @return Aligned pointer inside the arena.
     */
    [[nodiscard]] void* Alloc(std::size_t size,
                              std::size_t align = alignof(std::max_align_t)) noexcept;

    /**
     * @brief Reset the bump pointer to the start of the arena.
     *
     * Does NOT zero memory. After a reset the next Alloc starts fresh.
     * O(1) — simply sets the cursor back to zero.
     */
    void Reset() noexcept;

    // -----------------------------------------------------------------------
    // Introspection
    // -----------------------------------------------------------------------

    /// @return Total capacity in bytes.
    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

    /// @return Bytes currently allocated (since last Reset).
    [[nodiscard]] std::size_t Used() const noexcept { return m_cursor; }

    /// @return Bytes still available before the next overflow.
    [[nodiscard]] std::size_t Available() const noexcept {
        return m_capacity - m_cursor;
    }

private:
    uint8_t*    m_base     = nullptr; ///< Start of backing storage
    std::size_t m_capacity = 0;       ///< Total capacity in bytes
    std::size_t m_cursor   = 0;       ///< Current bump pointer offset
    MemTag      m_tag      = MemTag::Temp; ///< Tag used for backing storage
};

} // namespace engine::core::memory
