/**
 * @file Time.h
 * @brief Frame timing: delta, FPS sliding average, frame index. Uses monotonic clock (steady_clock).
 */

#pragma once

#include <cstdint>

namespace engine::core {

/** Default FPS window size for sliding average (e.g. 120 frames). */
constexpr int kDefaultFpsWindowSize = 120;

/**
 * Frame timing state. Call BeginFrame at start of frame, EndFrame at end.
 * Delta is clamped to avoid negative or runaway values.
 */
class Time {
public:
    /** Initialize with optional FPS window size (default 120). */
    explicit Time(int fpsWindowSize = kDefaultFpsWindowSize);

    /** Call at start of frame. Updates delta from previous frame. */
    void BeginFrame();

    /** Call at end of frame. Updates FPS sliding window. */
    void EndFrame();

    /** Delta time in seconds (clamped, never negative). */
    double DeltaSeconds() const { return m_deltaSeconds; }

    /** Current FPS (sliding average over the window). */
    double Fps() const { return m_fps; }

    /** Current frame index (incremented each BeginFrame). */
    uint64_t FrameIndex() const { return m_frameIndex; }

    /** Monotonic ticks in seconds (for "now" timing). */
    static double NowTicks();

private:
    static constexpr int kMaxFpsWindow = 256;
    int m_fpsWindowSize;
    double m_deltaSeconds = 0.0;
    double m_fps = 0.0;
    uint64_t m_frameIndex = 0;
    double m_prevTicks = 0.0;
    double m_frameTimes[kMaxFpsWindow]{};
    int m_frameTimeWrite = 0;
    int m_frameTimeCount = 0;
};

} // namespace engine::core
