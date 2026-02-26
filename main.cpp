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
 * M00.3 additions:
 *   - Jobs::Init() called after memory init.
 *   - Smoke tests: 1k jobs, 100k jobs, nested jobs, multi-producer.
 *   - Jobs::Shutdown() called at the end.
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

// M00.3 — Job System
#include "engine/core/jobs/JobSystem.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace engine::core;
using namespace engine::core::memory;
using namespace engine::core::jobs;

// ---------------------------------------------------------------------------
// Job System smoke tests (M00.3)
// ---------------------------------------------------------------------------

/**
 * @brief Test 1 — 1 000 simple jobs, all wait via a group.
 * @return true on success.
 */
static bool JobTest_1k() {
    constexpr int kCount = 1000;
    std::atomic<int> counter{ 0 };

    std::vector<std::function<void()>> fns;
    fns.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        fns.push_back([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    JobGroup group = Jobs::EnqueueGroup(std::move(fns));
    Jobs::Wait(group);

    return counter.load(std::memory_order_acquire) == kCount;
}

/**
 * @brief Test 2 — 100 000 jobs submitted and waited on via a group.
 * @return true on success.
 */
static bool JobTest_100k() {
    constexpr int kCount = 100000;
    std::atomic<int> counter{ 0 };

    std::vector<std::function<void()>> fns;
    fns.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        fns.push_back([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    auto t0 = std::chrono::steady_clock::now();
    JobGroup group = Jobs::EnqueueGroup(std::move(fns));
    Jobs::Wait(group);
    auto t1 = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    LOG_INFO(Jobs, "100k jobs completed in {:.1f} ms", ms);

    return counter.load(std::memory_order_acquire) == kCount;
}

/**
 * @brief Test 3 — nested jobs: each parent job submits a child job.
 * @return true on success.
 */
static bool JobTest_Nested() {
    constexpr int kOuter = 64;
    std::atomic<int> innerDone{ 0 };

    std::vector<std::function<void()>> outerFns;
    outerFns.reserve(kOuter);
    for (int i = 0; i < kOuter; ++i) {
        outerFns.push_back([&innerDone] {
            // Submit child from worker thread.
            JobHandle child = Jobs::Enqueue([&innerDone] {
                innerDone.fetch_add(1, std::memory_order_relaxed);
            });
            // Wait from inside worker (pump-based, no deadlock).
            Jobs::Wait(child);
        });
    }

    JobGroup group = Jobs::EnqueueGroup(std::move(outerFns));
    Jobs::Wait(group);

    return innerDone.load(std::memory_order_acquire) == kOuter;
}

/**
 * @brief Test 4 — multi-producer: several threads simultaneously enqueue jobs.
 * @return true on success.
 */
static bool JobTest_MultiProducer() {
    constexpr int kProducers  = 4;
    constexpr int kJobsEach   = 500;
    constexpr int kTotal      = kProducers * kJobsEach;

    std::atomic<int> counter{ 0 };
    std::vector<JobGroup> groups;
    std::mutex groupMutex;

    // Launch producer threads.
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            std::vector<std::function<void()>> fns;
            fns.reserve(kJobsEach);
            for (int j = 0; j < kJobsEach; ++j) {
                fns.push_back([&counter] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
            JobGroup g = Jobs::EnqueueGroup(std::move(fns));
            std::lock_guard<std::mutex> lk(groupMutex);
            groups.push_back(std::move(g));
        });
    }
    for (auto& t : producers) { t.join(); }

    // Wait for every group.
    for (auto& g : groups) { Jobs::Wait(g); }

    return counter.load(std::memory_order_acquire) == kTotal;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

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
    LOG_INFO(Core, "Engine starting — version 0.1.0 (M00.1 / M00.2 / M00.3)");

    // -----------------------------------------------------------------------
    // 3. Time
    // -----------------------------------------------------------------------
    Time::Init(/*maxDelta=*/0.1f, /*fpsWindow=*/120u);
    LOG_INFO(Core, "Time subsystem initialised");

    // -----------------------------------------------------------------------
    // 4. Memory — FrameArena[2] (M00.2)
    // -----------------------------------------------------------------------
    static constexpr std::size_t kFrameArenaBytes = 2u * 1024u * 1024u;
    FrameArena<2> frameArenas(kFrameArenaBytes, MemTag::Temp);
    LOG_INFO(Core, "FrameArena[2] initialised — {} KiB per slot",
             kFrameArenaBytes / 1024u);

    // -----------------------------------------------------------------------
    // 5. Job System (M00.3)
    // -----------------------------------------------------------------------
    // 0 = hardware_concurrency - 1 workers.
    Jobs::Init(0);
    LOG_INFO(Jobs, "JobSystem initialised");

    // --- Smoke tests ---
    LOG_INFO(Jobs, "Running job system smoke tests...");

    bool ok = true;

    ok &= JobTest_1k();
    LOG_INFO(Jobs, "  [{}] 1k jobs test", ok ? "PASS" : "FAIL");

    bool t100k = JobTest_100k();
    ok &= t100k;
    LOG_INFO(Jobs, "  [{}] 100k jobs test", t100k ? "PASS" : "FAIL");

    bool tNested = JobTest_Nested();
    ok &= tNested;
    LOG_INFO(Jobs, "  [{}] nested jobs test", tNested ? "PASS" : "FAIL");

    bool tMP = JobTest_MultiProducer();
    ok &= tMP;
    LOG_INFO(Jobs, "  [{}] multi-producer test", tMP ? "PASS" : "FAIL");

    if (ok) {
        LOG_INFO(Jobs, "All job system smoke tests PASSED.");
    } else {
        LOG_ERROR(Jobs, "One or more job system smoke tests FAILED.");
    }

    // -----------------------------------------------------------------------
    // 6. Minimal frame loop (abbreviated — no sleep to keep smoke test fast)
    // -----------------------------------------------------------------------
    constexpr int kTotalFrames     = 10;
    constexpr int kLogEveryNFrames = 5;

    for (int frame = 0; frame < kTotalFrames; ++frame) {
        Time::BeginFrame();
        frameArenas.BeginFrame(static_cast<uint64_t>(Time::FrameIndex()));

        [[maybe_unused]] void* scratch =
            frameArenas.Current().Alloc(256, alignof(float));

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
    // 7. Shutdown (reverse order)
    // -----------------------------------------------------------------------
    Jobs::Shutdown();
    LOG_INFO(Jobs, "JobSystem shut down");

    Memory::DumpStats();

    Log::Shutdown();
    Config::Shutdown();

    return ok ? 0 : 1;
}
