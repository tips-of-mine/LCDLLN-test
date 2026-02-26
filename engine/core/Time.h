#pragma once

/**
 * @file Time.h
 * @brief Frame timing utility: delta time, FPS (sliding window), frameIndex.
 *
 * Based on std::chrono::steady_clock (monotonic — never goes backward).
 *
 * Usage:
 *   Time::Init();
 *   // main loop:
 *   while (running) {
 *       Time::BeginFrame();
 *       float dt = Time::DeltaSeconds(); // clamped, never negative
 *       uint64_t fi = Time::FrameIndex();
 *       float fps = Time::FPS();
 *       Time::EndFrame();
 *   }
 */

#include <cstdint>
#include <chrono>

namespace engine::core {

/// Frame-timing subsystem.
class Time {
public:
    Time() = delete;

    /**
     * @brief Initialises the timing system.
     *
     * Records the base timestamp. Must be called once before any loop.
     *
     * @param maxDeltaSeconds  Upper clamp for delta (default 0.1 s = 100 ms).
     *                         Prevents large spikes when the application is
     *                         suspended or a breakpoint is hit.
     * @param fpsWindowSize    Number of frames in the sliding FPS average
     *                         (default 120).
     */
    static void Init(float   maxDeltaSeconds = 0.1f,
                     uint32_t fpsWindowSize  = 120u);

    /**
     * @brief Must be called at the START of every frame.
     *
     * Samples the current time, computes delta, updates FPS window, and
     * increments frameIndex.
     */
    static void BeginFrame();

    /**
     * @brief May be called at the END of every frame (reserved for future
     *        per-frame statistics; currently a no-op).
     */
    static void EndFrame() noexcept;

    // -----------------------------------------------------------------------
    // Accessors (valid after the first BeginFrame call)
    // -----------------------------------------------------------------------

    /// Delta time in seconds (clamped to [0, maxDeltaSeconds]).
    static float DeltaSeconds() noexcept;

    /// Delta time in milliseconds.
    static float DeltaMilliseconds() noexcept;

    /// Frames per second (sliding average over the configured window).
    static float FPS() noexcept;

    /// Total elapsed time in seconds since Init().
    static double ElapsedSeconds() noexcept;

    /// Monotonic frame counter, incremented once per BeginFrame.
    static uint64_t FrameIndex() noexcept;

    /**
     * @brief Returns the current value of the system monotonic clock in
     *        "ticks" (nanoseconds since epoch, implementation-defined).
     *
     * Useful for high-resolution profiling without going through the
     * Time subsystem's per-frame state.
     */
    static int64_t NowTicks() noexcept;
};

} // namespace engine::core
