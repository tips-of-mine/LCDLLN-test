// main.cpp — Engine entry point (minimal boot).
// Initialises Core (Config, Log, Time) then runs a minimal loop.

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"

#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    // 1. Config (CLI overrides have highest priority).
    engine::core::Config::Load("config.json", argc, argv);

    // 2. Logging.
    const std::string logFile  = engine::core::Config::GetString("log.file",  "engine.log");
    const std::string logLevel = engine::core::Config::GetString("log.level", "debug");

    engine::core::LogLevel minLevel = engine::core::LogLevel::Debug;
    if      (logLevel == "info")  { minLevel = engine::core::LogLevel::Info;  }
    else if (logLevel == "warn")  { minLevel = engine::core::LogLevel::Warn;  }
    else if (logLevel == "error") { minLevel = engine::core::LogLevel::Error; }

    engine::core::LogInit(logFile, minLevel);

    LOG_INFO(Core, "Engine v0.1.0 starting");
    LOG_INFO(Core, "Config loaded — content path: %s",
        engine::core::Config::GetString("paths.content", "game/data").c_str());

    // 3. Minimal frame loop — runs for 3 seconds then exits.
    constexpr float kRunSeconds  = 3.0f;
    float           elapsed      = 0.0f;
    float           fpsLogTimer  = 0.0f;

    while (elapsed < kRunSeconds) {
        engine::core::Time::BeginFrame();

        // Simulate ~60 Hz work.
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        engine::core::Time::EndFrame();

        const float dt = engine::core::Time::DeltaSeconds();
        elapsed      += dt;
        fpsLogTimer  += dt;

        // Log FPS once per second.
        if (fpsLogTimer >= 1.0f) {
            LOG_INFO(Core, "FPS: %.1f  frame: %llu  dt: %.4fs",
                engine::core::Time::FPS(),
                static_cast<unsigned long long>(engine::core::Time::FrameIndex()),
                dt);
            fpsLogTimer = 0.0f;
        }
    }

    LOG_INFO(Core, "Engine shutting down after %llu frames",
        static_cast<unsigned long long>(engine::core::Time::FrameIndex()));

    engine::core::LogShutdown();
    engine::core::Config::Shutdown();
    return 0;
}
