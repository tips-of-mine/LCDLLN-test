#pragma once

/**
 * @file FrameArena.h
 * @brief Per-frame linear arenas for double/triple-buffered frame data.
 *
 * FrameArena owns N LinearArenas (one per "frame in flight"). At the start of
 * each frame, the arena for that frame index is reset, providing scratch
 * memory for the entire frame with zero fragmentation.
 *
 * Typical use:
 *   FrameArena<2> frameArenas(2 * 1024 * 1024, MemTag::Render); // 2 MiB each
 *   // in BeginFrame(frameIndex):
 *   frameArenas.BeginFrame(frameIndex);
 *   void* p = frameArenas.Current().Alloc(64, 16);
 *
 * @tparam N  Number of frames in flight (2 or 3 are typical).
 */

#include "LinearArena.h"
#include "MemoryTags.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>

namespace engine::core::memory {

/// @brief N independent LinearArenas, one per frame-in-flight slot.
template<std::size_t N>
class FrameArena {
    static_assert(N >= 1 && N <= 8, "FrameArena: N must be in [1, 8]");

public:
    /**
     * @brief Construct N arenas, each with the given capacity.
     * @param capacityBytesEach Capacity of each arena in bytes.
     * @param tag               MemTag for the backing allocations.
     */
    explicit FrameArena(std::size_t capacityBytesEach,
                        MemTag      tag = MemTag::Temp)
    {
        for (std::size_t i = 0; i < N; ++i) {
            m_arenas[i] = std::make_unique<LinearArena>(capacityBytesEach, tag);
        }
    }

    // Non-copyable, non-movable.
    FrameArena(const FrameArena&)            = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&&)                 = delete;
    FrameArena& operator=(FrameArena&&)      = delete;

    /**
     * @brief Must be called at the beginning of each frame.
     *
     * Selects the arena slot for `frameIndex` (using `frameIndex % N`) and
     * resets it so allocations for this frame start from the top.
     *
     * @param frameIndex Monotonically increasing frame counter.
     */
    void BeginFrame(uint64_t frameIndex) noexcept {
        m_current = static_cast<std::size_t>(frameIndex % N);
        m_arenas[m_current]->Reset();
    }

    /**
     * @brief Return the arena for the current frame.
     *
     * Call BeginFrame() before using this.
     */
    [[nodiscard]] LinearArena& Current() noexcept {
        return *m_arenas[m_current];
    }

    /**
     * @brief Return the arena at a specific slot (0-indexed).
     * @param slot Index in [0, N).
     */
    [[nodiscard]] LinearArena& Arena(std::size_t slot) noexcept {
        assert(slot < N && "FrameArena::Arena: slot out of range");
        return *m_arenas[slot];
    }

    /// @return Number of frame slots (N).
    [[nodiscard]] static constexpr std::size_t Slots() noexcept { return N; }

private:
    std::array<std::unique_ptr<LinearArena>, N> m_arenas{};
    std::size_t m_current = 0; ///< Slot index for the current frame
};

} // namespace engine::core::memory
