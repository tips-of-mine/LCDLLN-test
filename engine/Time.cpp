// engine/core/Time.cpp
// Frame timing implementation using std::chrono::steady_clock.

#include "Time.h"

#include <algorithm>
#include <cstring>

namespace engine::core {

// ─── Static member definitions ───────────────────────────────────────────────

Time::TimePoint Time::s_beginTime   = Time::Clock::now();
Time::TimePoint Time::s_frameStart  = Time::Clock::now();
float           Time::s_deltaSeconds = 0.0f;
float           Time::s_fpsWindow[Time::kFpsWindowSize] = {};
int             Time::s_fpsWriteIdx = 0;
int             Time::s_fpsCount    = 0;
float           Time::s_fpsAccum    = 0.0f;
uint64_t        Time::s_frameIndex  = 0;

// ─── Frame lifecycle ──────────────────────────────────────────────────────────

void Time::BeginFrame() noexcept {
    s_frameStart = Clock::now();
}

void Time::EndFrame() noexcept {
    const auto   now      = Clock::now();
    const double elapsed  = std::chrono::duration<double>(now - s_frameStart).count();

    // Clamp delta: never negative (steady_clock guarantees monotonic), and
    // bounded above to avoid the spiral-of-death on long stalls.
    s_deltaSeconds = static_cast<float>(
        std::max(0.0, std::min(elapsed, static_cast<double>(kMaxDeltaSeconds)))
    );

    // Update sliding FPS window.
    // Accumulate the reciprocal (1/delta == instant FPS for this frame).
    const float instantFps = (s_deltaSeconds > 0.0f) ? (1.0f / s_deltaSeconds) : 0.0f;

    // Subtract the old value leaving the slot, add the new one.
    s_fpsAccum -= s_fpsWindow[s_fpsWriteIdx];
    s_fpsWindow[s_fpsWriteIdx] = instantFps;
    s_fpsAccum += instantFps;
    s_fpsWriteIdx = (s_fpsWriteIdx + 1) % kFpsWindowSize;
    if (s_fpsCount < kFpsWindowSize) { ++s_fpsCount; }

    ++s_frameIndex;
}

// ─── Accessors ────────────────────────────────────────────────────────────────

float Time::DeltaSeconds() noexcept {
    return s_deltaSeconds;
}

float Time::FPS() noexcept {
    if (s_fpsCount == 0) { return 0.0f; }
    return s_fpsAccum / static_cast<float>(s_fpsCount);
}

uint64_t Time::FrameIndex() noexcept {
    return s_frameIndex;
}

int64_t Time::NowTicks() noexcept {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace engine::core
