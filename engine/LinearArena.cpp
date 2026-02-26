// engine/core/memory/LinearArena.cpp
// M00.2 — LinearArena implementation.

#include "LinearArena.h"

#include <cassert>
#include <cstdint>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Align @p value up to the next multiple of @p alignment (power-of-two).
static inline size_t alignUp(size_t value, size_t alignment) noexcept {
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
    return (value + alignment - 1) & ~(alignment - 1);
}

// ---------------------------------------------------------------------------
// LinearArena API
// ---------------------------------------------------------------------------

void LinearArena::init(void* buffer, size_t capacity, MemTag tag) noexcept {
    assert(buffer   != nullptr && "LinearArena::init – null buffer");
    assert(capacity  > 0       && "LinearArena::init – zero capacity");
    m_buffer   = static_cast<uint8_t*>(buffer);
    m_capacity = capacity;
    m_offset   = 0;
    m_tag      = tag;
}

void* LinearArena::alloc(size_t size, size_t alignment) noexcept {
    assert(m_buffer  != nullptr && "LinearArena not initialised");
    assert(size       > 0       && "LinearArena::alloc – zero size");

    const size_t aligned = alignUp(m_offset, alignment);
    const size_t newOffset = aligned + size;

    // Overflow guard – hard assert: arena exhaustion is a programming error.
    assert(newOffset <= m_capacity && "LinearArena overflow – increase capacity");

    void* ptr  = m_buffer + aligned;
    m_offset   = newOffset;
    return ptr;
}

void LinearArena::reset() noexcept {
    m_offset = 0;
}

} // namespace engine::core::memory
