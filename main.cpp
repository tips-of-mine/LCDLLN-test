/**
 * @file main.cpp
 * @brief Minimal boot entry point.
 *
 * Initialises core subsystems (Config, Log, Time) and runs the main loop
 * as described by ticket M00.1.
 *
 * M00.2 additions:
 *   - FrameArena[2] constructed before the loop.
 *   - FrameArena::BeginFrame() called at the start of each iteration.
 *   - Memory::DumpStats() called at shutdown.
 *
 * The main function is intentionally kept minimal per project conventions.
 */

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"

// M00.2 — Memory subsystem
#include "engine/core/memory/FrameArena.h"
#include "engine/core/memory/Memory.h"
#include "engine/core/memory/MemoryTags.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace engine::core;
using namespace engine::core::memory;

int main(int argc, const char* argv[]) {
    // -----------------------------------------------------------------------
    // 1. Config (must be first so other subsystems can read settings)
    // -----------------------------------------------------------------------
    Config::Init("config.json", argc, argv);

    // -----------------------------------------------------------------------
    // 2. Logging
    // -----------------------------------------------------------------------
    const std::string logFile  = Config::GetString("log.file",  "engine.log");
    const std::string levelStr = Config::GetString("log.level", "DEBUG");

    LogLevel logLevel = LogLevel::Debug;
    if      (levelStr == "TRACE")   { logLevel = LogLevel::Trace;   }
    else if (levelStr == "DEBUG")   { logLevel = LogLevel::Debug;   }
    else if (levelStr == "INFO")    { logLevel = LogLevel::Info;    }
    else if (levelStr == "WARNING") { logLevel = LogLevel::Warning; }
    else if (levelStr == "ERROR")   { logLevel = LogLevel::Error;   }

    Log::Init(logFile, logLevel);
    LOG_INFO(Core, "Engine starting — version 0.1.0 (M00.1 / M00.2)");

    // -----------------------------------------------------------------------
    // 3. Time
    // -----------------------------------------------------------------------
    Time::Init(/*maxDelta=*/0.1f, /*fpsWindow=*/120u);
    LOG_INFO(Core, "Time subsystem initialised");

    // -----------------------------------------------------------------------
    // 4. Memory — FrameArena[2] (M00.2)
    // -----------------------------------------------------------------------
    // 2 MiB per frame slot; tagged as Temp for transient per-frame data.
    static constexpr std::size_t kFrameArenaBytes = 2u * 1024u * 1024u;
    FrameArena<2> frameArenas(kFrameArenaBytes, MemTag::Temp);
    LOG_INFO(Core, "FrameArena[2] initialised — {} KiB per slot",
             kFrameArenaBytes / 1024u);

    // -----------------------------------------------------------------------
    // 5. Minimal smoke-test loop (runs for a short time then exits)
    // -----------------------------------------------------------------------
    constexpr int kTotalFrames     = 300;
    constexpr int kLogEveryNFrames = 60;

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        Time::BeginFrame();

        // M00.2: reset the frame arena for this frame slot.
        frameArenas.BeginFrame(static_cast<uint64_t>(Time::FrameIndex()));

        // Example scratch allocation from the frame arena (illustrative).
        // In a real frame this would feed render data, dynamic strings, etc.
        [[maybe_unused]] void* scratch =
            frameArenas.Current().Alloc(256, alignof(float));

        // Simulate ~60 Hz workload.
        std::this_thread::sleep_for(std::chrono::microseconds(16666));

        if ((Time::FrameIndex() % static_cast<uint64_t>(kLogEveryNFrames)) == 0) {
            LOG_INFO(Core,
                     "Frame {:>6} | dt={:.3f}ms | FPS={:.1f} | elapsed={:.2f}s",
                     Time::FrameIndex(),
                     Time::DeltaMilliseconds(),
                     Time::FPS(),
                     Time::ElapsedSeconds());
        }

        Time::EndFrame();
    }

    LOG_INFO(Core, "Smoke test complete — {} frames rendered", kTotalFrames);

    // -----------------------------------------------------------------------
    // 6. Memory stats dump (M00.2)
    // -----------------------------------------------------------------------
    Memory::DumpStats();

    // -----------------------------------------------------------------------
    // 7. Shutdown
    // -----------------------------------------------------------------------
    Log::Shutdown();
    Config::Shutdown();

    return 0;
}
