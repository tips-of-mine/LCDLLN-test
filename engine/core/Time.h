#pragma once

#include <chrono>
#include <cstdint>
#include <deque>

namespace engine::core {

class Time {
public:
    static void BeginFrame();
    static void EndFrame();

    static double DeltaSeconds();
    static double Fps();
    static std::uint64_t FrameIndex();
    static std::uint64_t NowTicks();

private:
    static constexpr double kMaxDeltaSeconds = 0.25;
    static constexpr std::size_t kFpsWindowSize = 120;

    static std::chrono::steady_clock::time_point s_lastFrameStart;
    static std::chrono::steady_clock::time_point s_frameStart;
    static std::deque<double> s_recentFrameTimes;
    static double s_deltaSeconds;
    static double s_fps;
    static std::uint64_t s_frameIndex;
    static bool s_initialized;
};

} // namespace engine::core
