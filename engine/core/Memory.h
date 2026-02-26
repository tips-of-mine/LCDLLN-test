#pragma once

/**
 * @file Memory.h
 * @brief Top-level Memory API.
 *
 * Provides the generic alloc/free functions and DumpStats for logging
 * all per-tag statistics to stdout (and the engine log if available).
 *
 * Convenience typed helpers are also supplied:
 *   T* p = Memory::New<T>(MemTag::Render, args...);
 *   Memory::Delete(p, MemTag::Render);
 */

#include "MemoryTags.h"
#include "SystemAllocator.h"

#include <cstddef>
#include <new>          // placement new
#include <utility>      // std::forward

namespace engine::core::memory {

/// @brief Namespace-level memory utilities (alloc, free, stats dump).
struct Memory {
    Memory() = delete;

    // -----------------------------------------------------------------------
    // Raw allocation API
    // -----------------------------------------------------------------------

    /**
     * @brief Allocate `size` bytes with `align` alignment, tagged.
     *
     * Delegates to SystemAllocator; asserts on failure.
     *
     * @param size  Bytes to allocate (must be > 0).
     * @param align Alignment (power of two, >= 1).
     * @param tag   Subsystem tag.
     * @return Non-null pointer on success.
     */
    [[nodiscard]]
    static void* Alloc(std::size_t size,
                       std::size_t align = alignof(std::max_align_t),
                       MemTag      tag   = MemTag::Core) noexcept;

    /**
     * @brief Free memory previously obtained from Memory::Alloc.
     *
     * @param ptr   Pointer (nullptr is a no-op).
     * @param size  Original allocation size.
     * @param tag   Same tag used for the allocation.
     */
    static void Free(void*       ptr,
                     std::size_t size,
                     MemTag      tag = MemTag::Core) noexcept;

    // -----------------------------------------------------------------------
    // Typed helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Allocate and construct a single object of type T.
     *
     * @tparam T     Type to construct.
     * @param  tag   MemTag for the allocation.
     * @param  args  Constructor arguments forwarded to T.
     * @return Pointer to the newly constructed T.
     */
    template<typename T, typename... Args>
    [[nodiscard]]
    static T* New(MemTag tag, Args&&... args) {
        void* mem = SystemAllocator::Alloc(sizeof(T), alignof(T), tag);
        return new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * @brief Destruct and free a single object of type T.
     *
     * @tparam T   Type to destroy.
     * @param  ptr Pointer returned by Memory::New<T>.
     * @param  tag Same tag used for the allocation.
     */
    template<typename T>
    static void Delete(T* ptr, MemTag tag) noexcept {
        if (ptr == nullptr) { return; }
        ptr->~T();
        SystemAllocator::Free(ptr, sizeof(T), tag);
    }

    // -----------------------------------------------------------------------
    // Statistics
    // -----------------------------------------------------------------------

    /**
     * @brief Print a formatted statistics table for all MemTags.
     *
     * Output format (one row per tag):
     *   [MEMORY] Tag=<name>  current=<N> B  peak=<N> B  allocs=<N>  frees=<N>
     *
     * Writes to stdout. If the engine Log subsystem is available, callers
     * should redirect output there; DumpStats intentionally uses printf-style
     * output to avoid a hard dependency on Log (Memory is a low-level module).
     */
    static void DumpStats();
};

} // namespace engine::core::memory
