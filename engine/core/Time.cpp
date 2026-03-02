/**
 * @file Time.cpp
 * @brief Implementation of frame timing using steady_clock.
 */

#include "engine/core/Time.h"
#include <chrono>
#include <algorithm>

namespace engine::core {

namespace {

using Clock = std::chrono::steady_clock;

double TicksFromClock() {
    auto t = Clock::now().time_since_epoch();
    return std::chrono::duration<double>(t).count();
}

} // namespace

Time::Time(int fpsWindowSize)
    : m_fpsWindowSize(std::max(1, fpsWindowSize)) {
    m_prevTicks = TicksFromClock();
}

void Time::BeginFrame() {
    double now = TicksFromClock();
    m_deltaSeconds = now - m_prevTicks;
    m_prevTicks = now;
    // Clamp: never negative, cap at e.g. 0.5s to avoid spikes
    if (m_deltaSeconds < 0.0) m_deltaSeconds = 0.0;
    if (m_deltaSeconds > 0.5) m_deltaSeconds = 0.5;
    ++m_frameIndex;
}

void Time::EndFrame() {
    if (m_deltaSeconds > 0.0) {
        m_frameTimes[m_frameTimeWrite % m_fpsWindowSize] = m_deltaSeconds;
        ++m_frameTimeWrite;
        if (m_frameTimeCount < m_fpsWindowSize) ++m_frameTimeCount;
        double sum = 0.0;
        int n = m_frameTimeCount;
        for (int i = 0; i < n; ++i) {
            int idx = (m_frameTimeWrite - 1 - i + m_fpsWindowSize) % m_fpsWindowSize;
            sum += m_frameTimes[idx];
        }
        double avgDelta = sum / n;
        m_fps = (avgDelta > 0.0) ? (1.0 / avgDelta) : 0.0;
    }
}

double Time::NowTicks() {
    return TicksFromClock();
}

} // namespace engine::core
