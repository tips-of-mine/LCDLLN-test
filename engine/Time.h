#pragma once
// engine/core/Time.h
// Frame timing utilities.
//  - Uses std::chrono::steady_clock (monotonic, never negative delta).
//  - FPS computed as a sliding window average over kFpsWindowSize frames.
//  - Delta is clamped to avoid spiral-of-death on long stalls.
// Usage:
//   Time::BeginFrame();
//   // ... do work ...
//   Time::EndFrame();
//   float dt = Time::DeltaSeconds();

#include <cstdint>
#include <chrono>

namespace engine::core {

class Time {
public:
    /// Number of frames in the FPS sliding window.
    static constexpr int kFpsWindowSize = 120;

    /// Maximum allowed delta in seconds (prevents spiral-of-death on stall).
    static constexpr float kMaxDeltaSeconds = 0.25f;

    // ── Frame lifecycle ───────────────────────────────────────────────────

    /// Call once at the start of each frame (before update/render).
    static void BeginFrame() noexcept;

    /// Call once at the end of each frame (after render, before present).
    /// Computes delta and updates FPS window.
    static void EndFrame() noexcept;

    // ── Accessors ─────────────────────────────────────────────────────────

    /// Time since the previous frame, in seconds. Never negative, clamped.
    static float DeltaSeconds() noexcept;

    /// Sliding-window FPS average (last kFpsWindowSize frames).
    static float FPS() noexcept;

    /// Monotonically increasing frame counter (starts at 0).
    static uint64_t FrameIndex() noexcept;

    /// Current tick count (nanoseconds since steady_clock epoch).
    /// Suitable for profiling; do not compare across processes.
    static int64_t NowTicks() noexcept;

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static TimePoint s_beginTime;
    static TimePoint s_frameStart;
    static float     s_deltaSeconds;
    static float     s_fpsWindow[kFpsWindowSize];
    static int       s_fpsWriteIdx;
    static int       s_fpsCount;
    static float     s_fpsAccum;
    static uint64_t  s_frameIndex;
};

} // namespace engine::core
