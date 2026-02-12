#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace engine::core::memory {

enum class MemTag : std::uint8_t {
    Core = 0,
    Render,
    Assets,
    World,
    Net,
    UI,
    Tools,
    Temp,
    Count
};

constexpr std::size_t kMemTagCount = static_cast<std::size_t>(MemTag::Count);

inline constexpr std::array<const char*, kMemTagCount> kMemTagNames = {
    "Core",
    "Render",
    "Assets",
    "World",
    "Net",
    "UI",
    "Tools",
    "Temp"
};

struct MemTagStats {
    std::atomic<std::size_t> currentBytes{0};
    std::atomic<std::size_t> peakBytes{0};
    std::atomic<std::uint64_t> allocCount{0};
    std::atomic<std::uint64_t> freeCount{0};
};

} // namespace engine::core::memory
