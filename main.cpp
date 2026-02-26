/**
 * @file main.cpp
 * @brief Minimal boot entry point.
 *
 * Initialises core subsystems (Config, Log, Time) and runs the main loop
 * as described by ticket M00.1.
 *
 * The main function is intentionally kept minimal per project conventions.
 */

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace engine::core;

int main(int argc, const char* argv[]) {
    // -----------------------------------------------------------------------
    // 1. Config (must be first so other subsystems can read settings)
    // -----------------------------------------------------------------------
    Config::Init("config.json", argc, argv);

    // -----------------------------------------------------------------------
    // 2. Logging
    // -----------------------------------------------------------------------
    const std::string logFile   = Config::GetString("log.file",  "engine.log");
    const std::string levelStr  = Config::GetString("log.level", "DEBUG");

    LogLevel logLevel = LogLevel::Debug;
    if      (levelStr == "TRACE")   { logLevel = LogLevel::Trace;   }
    else if (levelStr == "DEBUG")   { logLevel = LogLevel::Debug;   }
    else if (levelStr == "INFO")    { logLevel = LogLevel::Info;    }
    else if (levelStr == "WARNING") { logLevel = LogLevel::Warning; }
    else if (levelStr == "ERROR")   { logLevel = LogLevel::Error;   }

    Log::Init(logFile, logLevel);
    LOG_INFO(Core, "Engine starting — version 0.1.0 (M00.1)");

    // -----------------------------------------------------------------------
    // 3. Time
    // -----------------------------------------------------------------------
    Time::Init(/*maxDelta=*/0.1f, /*fpsWindow=*/120u);
    LOG_INFO(Core, "Time subsystem initialised");

    // -----------------------------------------------------------------------
    // 4. Minimal smoke-test loop (runs for a short time then exits)
    // -----------------------------------------------------------------------
    // In a real engine this would be the window/render/game loop.
    // Here we run 300 frames (~5 s at 60 Hz) and log FPS every ~1 s.

    constexpr int kTotalFrames      = 300;
    constexpr int kLogEveryNFrames  = 60; // approximately 1 s worth

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        Time::BeginFrame();

        // Simulate ~60 Hz workload.
        std::this_thread::sleep_for(std::chrono::microseconds(16666));

        if ((Time::FrameIndex() % kLogEveryNFrames) == 0) {
            LOG_INFO(Core,
                     "Frame {:>6} | dt={:.3f}ms | FPS={:.1f} | elapsed={:.2f}s",
                     Time::FrameIndex(),
                     Time::DeltaMilliseconds(),
                     Time::FPS(),
                     Time::ElapsedSeconds());
        }

        Time::EndFrame();
    }

    LOG_INFO(Core, "Smoke test complete — {:} frames rendered", kTotalFrames);

    // -----------------------------------------------------------------------
    // 5. Shutdown
    // -----------------------------------------------------------------------
    Log::Shutdown();
    Config::Shutdown();

    return 0;
}
