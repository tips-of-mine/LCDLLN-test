/**
 * @file Time.cpp
 * @brief Implementation of the frame-timing subsystem.
 *
 * Uses std::chrono::steady_clock (monotonic) throughout.
 * The FPS sliding window is a fixed-capacity circular buffer of per-frame
 * durations; average FPS = windowSize / sum(durations).
 */

#include "Time.h"

#include <algorithm>
#include <cassert>
#include <vector>

namespace engine::core {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = std::chrono::duration<double>; // seconds as double

/// Configuration (set once in Init).
float    g_maxDelta   = 0.1f;
uint32_t g_windowSize = 120u;

/// Timing state.
TimePoint g_initTime;
TimePoint g_lastFrameTime;
float     g_delta        = 0.0f;  ///< Clamped delta for current frame (seconds).
double    g_elapsed      = 0.0;   ///< Total elapsed seconds since Init.
uint64_t  g_frameIndex   = 0ull;

/// Sliding FPS window (circular buffer of frame durations in seconds).
std::vector<float> g_fpsWindow;
uint32_t           g_fpsHead    = 0u;
float              g_fpsSum     = 0.0f;   ///< Sum of durations in window.
uint32_t           g_fpsCount   = 0u;     ///< Number of valid entries (<= windowSize).

} // namespace

// ---------------------------------------------------------------------------
// Time public API
// ---------------------------------------------------------------------------

void Time::Init(float maxDeltaSeconds, uint32_t fpsWindowSize) {
    g_maxDelta   = maxDeltaSeconds;
    g_windowSize = (fpsWindowSize > 0u) ? fpsWindowSize : 1u;

    g_fpsWindow.assign(g_windowSize, 0.0f);
    g_fpsHead  = 0u;
    g_fpsSum   = 0.0f;
    g_fpsCount = 0u;

    g_frameIndex    = 0ull;
    g_delta         = 0.0f;
    g_elapsed       = 0.0;

    g_initTime      = Clock::now();
    g_lastFrameTime = g_initTime;
}

void Time::BeginFrame() {
    const TimePoint now = Clock::now();

    // Raw delta in seconds.
    const Duration rawDelta = now - g_lastFrameTime;
    const float rawDeltaF   = static_cast<float>(rawDelta.count());

    // Clamp: never negative, never larger than maxDelta.
    g_delta = std::clamp(rawDeltaF, 0.0f, g_maxDelta);

    // Update elapsed (use raw, un-clamped for wall-clock accuracy).
    g_elapsed = Duration(now - g_initTime).count();

    // Update FPS sliding window.
    // We store raw (un-clamped) delta so FPS reflects real timing.
    // Guard against division by zero: skip frames where rawDelta == 0.
    if (rawDeltaF > 0.0f) {
        // Remove the oldest value from the sum.
        g_fpsSum -= g_fpsWindow[g_fpsHead];
        // Insert new value.
        g_fpsWindow[g_fpsHead] = rawDeltaF;
        g_fpsSum += rawDeltaF;
        g_fpsHead = (g_fpsHead + 1u) % g_windowSize;
        if (g_fpsCount < g_windowSize) { ++g_fpsCount; }
    }

    g_lastFrameTime = now;
    ++g_frameIndex;
}

void Time::EndFrame() noexcept {
    // Reserved for future per-frame GPU/CPU statistics.
}

float Time::DeltaSeconds() noexcept {
    return g_delta;
}

float Time::DeltaMilliseconds() noexcept {
    return g_delta * 1000.0f;
}

float Time::FPS() noexcept {
    if (g_fpsCount == 0u || g_fpsSum <= 0.0f) { return 0.0f; }
    return static_cast<float>(g_fpsCount) / g_fpsSum;
}

double Time::ElapsedSeconds() noexcept {
    return g_elapsed;
}

uint64_t Time::FrameIndex() noexcept {
    return g_frameIndex;
}

int64_t Time::NowTicks() noexcept {
    return Clock::now().time_since_epoch().count();
}

} // namespace engine::core
