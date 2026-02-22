#pragma once

#include "engine/core/memory/MemoryTags.h"

#include <array>
#include <cstddef>

namespace Memory {

struct AllocationHeader {
    void* rawPtr{nullptr};
    std::size_t size{0};
};

struct SystemAllocator {
    static void* Alloc(std::size_t size, std::size_t alignment, MemTag tag);
    static void Free(void* ptr, MemTag tag);
};

void* Alloc(std::size_t size, std::size_t alignment, MemTag tag);
void Free(void* ptr, MemTag tag);

class LinearArena {
public:
    LinearArena() = default;
    explicit LinearArena(std::size_t size, MemTag tag);

    void Initialize(std::size_t size, MemTag tag);
    void Destroy();

    void* Allocate(std::size_t size, std::size_t alignment);
    void Reset();

    std::size_t Capacity() const { return m_size; }
    std::size_t Offset() const { return m_offset; }

private:
    std::byte* m_base{nullptr};
    std::size_t m_size{0};
    std::size_t m_offset{0};
    MemTag m_tag{MemTag::Temp};
};

using FrameArena = LinearArena;

void InitializeFrameArenas(std::size_t perFrameSize, int framesInFlight, MemTag tag = MemTag::Temp);
void ShutdownFrameArenas();

void BeginFrame(int frameIndex);
FrameArena& GetFrameArena(int frameIndex);
int FramesInFlight();

void DumpStats();

} // namespace Memory
