/**
 * @file main.cpp
 * @brief Minimal boot: init Log/Config/Time, log version, FPS once per second.
 */

#include "engine/core/Log.h"
#include "engine/core/Config.h"
#include "engine/core/memory/Memory.h"
#include "engine/core/jobs/JobSystem.h"
#include "engine/core/Engine.h"
#include "engine/platform/Window.h"
#include "engine/platform/Input.h"
#include "engine/platform/FileSystem.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    engine::core::LogInit("engine.log", engine::core::LogLevel::Debug);
    engine::core::Config::Load("config.json");
    engine::core::Config::ApplyArgs(argc, argv);

    engine::core::Memory::Init(engine::core::kDefaultFramesInFlight, engine::core::kDefaultFrameArenaCapacity);
    engine::core::Jobs::Init(0);

    std::string version = engine::core::Config::GetString("version", "0.1");
    LOG_INFO(Core, "Engine version %s", version.c_str());

    engine::platform::FileSystem::SetContentRoot(engine::core::Config::GetString("paths.content", "game/data"));
    if (engine::platform::FileSystem::Exists(".")) {
        auto list = engine::platform::FileSystem::List(".");
        if (!list.empty()) {
            std::string sample = engine::platform::FileSystem::ReadAllText(list[0]);
            LOG_INFO(Core, "FS: read %zu bytes from %s", sample.size(), list[0].c_str());
        }
    }

    {
        std::atomic<uint64_t> counter{0};
        engine::core::JobGroup group;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100000; ++i) {
            engine::core::Jobs::EnqueueToGroup(&group, [&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        engine::core::Jobs::Wait(&group);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        LOG_INFO(Core, "Jobs: 100k done in %lld ms, count=%llu", static_cast<long long>(ms), static_cast<unsigned long long>(counter.load()));
    }
    {
        std::atomic<bool> innerDone{false};
        auto outer = engine::core::Jobs::Enqueue([&innerDone]() {
            auto inner = engine::core::Jobs::Enqueue([&innerDone]() { innerDone.store(true); });
            engine::core::Jobs::Wait(inner);
        });
        engine::core::Jobs::Wait(outer);
        LOG_INFO(Core, "Jobs: nested Wait-from-worker OK");
    }

    engine::platform::Window window;
    if (!window.Create("Engine", 1280, 720)) {
        LOG_ERROR(Core, "Window create failed");
        engine::core::Jobs::Shutdown();
        engine::core::LogShutdown();
        return 1;
    }
    engine::platform::Input input;
    engine::core::Engine engine;
    engine.SetOnResize([](int w, int h) { LOG_INFO(Core, "Resize %d x %d", w, h); });
    engine.Run(&window, &input);
    window.Destroy();

    engine::core::Jobs::Shutdown();
    engine::core::Memory::DumpStats();
    engine::core::Memory::Shutdown();
    engine::core::LogShutdown();
    return 0;
}
