#include "engine/core/Time.h"

#include <algorithm>

namespace engine::core {

std::chrono::steady_clock::time_point Time::s_lastFrameStart{};
std::chrono::steady_clock::time_point Time::s_frameStart{};
std::deque<double> Time::s_recentFrameTimes{};
double Time::s_deltaSeconds = 0.0;
double Time::s_fps = 0.0;
std::uint64_t Time::s_frameIndex = 0;
bool Time::s_initialized = false;

void Time::BeginFrame() {
    s_frameStart = std::chrono::steady_clock::now();
    if (!s_initialized) {
        s_lastFrameStart = s_frameStart;
        s_deltaSeconds = 0.0;
        s_initialized = true;
    } else {
        const auto rawDelta = std::chrono::duration<double>(s_frameStart - s_lastFrameStart).count();
        s_deltaSeconds = std::clamp(rawDelta, 0.0, kMaxDeltaSeconds);
        s_lastFrameStart = s_frameStart;
    }

    ++s_frameIndex;
}

void Time::EndFrame() {
    const auto frameEnd = std::chrono::steady_clock::now();
    const double frameTime = std::max(0.0, std::chrono::duration<double>(frameEnd - s_frameStart).count());

    s_recentFrameTimes.push_back(frameTime);
    if (s_recentFrameTimes.size() > kFpsWindowSize) {
        s_recentFrameTimes.pop_front();
    }

    double total = 0.0;
    for (const double value : s_recentFrameTimes) {
        total += value;
    }

    s_fps = (total > 0.0) ? static_cast<double>(s_recentFrameTimes.size()) / total : 0.0;
}

double Time::DeltaSeconds() {
    return s_deltaSeconds;
}

double Time::Fps() {
    return s_fps;
}

std::uint64_t Time::FrameIndex() {
    return s_frameIndex;
}

std::uint64_t Time::NowTicks() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

} // namespace engine::core
