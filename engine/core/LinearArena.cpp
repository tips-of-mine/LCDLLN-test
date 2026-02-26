/**
 * @file LinearArena.cpp
 * @brief LinearArena implementation.
 */

#include "LinearArena.h"
#include "SystemAllocator.h"

#include <cassert>
#include <cstdint>

namespace engine::core::memory {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LinearArena::LinearArena(std::size_t capacityBytes, MemTag tag)
    : m_capacity(capacityBytes)
    , m_cursor(0)
    , m_tag(tag)
{
    assert(capacityBytes > 0 && "LinearArena: capacity must be > 0");

    // Allocate backing storage aligned to the maximum natural alignment so
    // that any type can be stored without additional offset.
    m_base = static_cast<uint8_t*>(
        SystemAllocator::Alloc(capacityBytes, alignof(std::max_align_t), tag));
}

LinearArena::~LinearArena() {
    SystemAllocator::Free(m_base, m_capacity, m_tag);
    m_base   = nullptr;
    m_cursor = 0;
}

// ---------------------------------------------------------------------------
// Alloc
// ---------------------------------------------------------------------------

void* LinearArena::Alloc(std::size_t size, std::size_t align) noexcept {
    assert(size  > 0 && "LinearArena::Alloc: size must be > 0");
    assert(align > 0 && (align & (align - 1u)) == 0u &&
           "LinearArena::Alloc: align must be a power of two");

    // Round cursor up to the requested alignment.
    const std::size_t alignMask = align - 1u;
    const std::size_t aligned   = (m_cursor + alignMask) & ~alignMask;
    const std::size_t newCursor = aligned + size;

    assert(newCursor <= m_capacity &&
           "LinearArena::Alloc: arena overflow — increase capacity");

    m_cursor = newCursor;
    return m_base + aligned;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void LinearArena::Reset() noexcept {
    m_cursor = 0;
}

} // namespace engine::core::memory
