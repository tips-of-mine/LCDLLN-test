#pragma once

#include "engine/core/memory/MemoryTags.h"

#include <array>
#include <cstddef>

namespace engine::core::memory {

class SystemAllocator {
public:
    static void* Alloc(std::size_t size, std::size_t alignment, MemTag tag);
    static void Free(void* ptr, MemTag tag);

    static void DumpStats();

private:
    static void TrackAlloc(MemTag tag, std::size_t size);
    static void TrackFree(MemTag tag, std::size_t size);

    static std::array<MemTagStats, kMemTagCount> s_stats;
};

class LinearArena {
public:
    LinearArena() = default;
    explicit LinearArena(std::size_t capacity, MemTag backingTag = MemTag::Temp);
    ~LinearArena();

    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;

    LinearArena(LinearArena&& other) noexcept;
    LinearArena& operator=(LinearArena&& other) noexcept;

    void Initialize(std::size_t capacity, MemTag backingTag = MemTag::Temp);
    void Shutdown();

    void* Alloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    void Reset();

    [[nodiscard]] std::size_t Capacity() const;
    [[nodiscard]] std::size_t Used() const;

private:
    std::byte* m_buffer = nullptr;
    std::size_t m_capacity = 0;
    std::size_t m_offset = 0;
    MemTag m_backingTag = MemTag::Temp;
};

class Memory {
public:
    static constexpr std::size_t kFramesInFlight = 2;

    static void InitializeFrameArenas(std::size_t bytesPerFrameArena = 1024 * 1024);
    static void ShutdownFrameArenas();

    static void BeginFrame();
    static LinearArena& CurrentFrameArena();

    static void DumpStats();

private:
    static std::array<LinearArena, kFramesInFlight> s_frameArenas;
    static std::size_t s_currentFrameArena;
    static bool s_initialized;
};

} // namespace engine::core::memory
