#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/core/memory/Memory.h"

#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    engine::core::Config config;
    config.SetDefault("log.level", "info");
    config.SetDefault("log.file", "engine.log");
    config.SetDefault("log.flush", "true");
    config.SetDefault("app.version", "0.1.0");
    config.SetDefault("paths.content", "game/data");

    config.LoadFromFile("config.json");
    config.ApplyCommandLine(argc, argv);

    engine::core::LogLevel level = engine::core::LogLevel::Info;
    const std::string levelText = config.GetString("log.level", "info");
    if (levelText == "trace") level = engine::core::LogLevel::Trace;
    else if (levelText == "debug") level = engine::core::LogLevel::Debug;
    else if (levelText == "warn") level = engine::core::LogLevel::Warn;
    else if (levelText == "error") level = engine::core::LogLevel::Error;
    else if (levelText == "fatal") level = engine::core::LogLevel::Fatal;

    engine::core::Log::Init({
        .minLevel = level,
        .filePath = config.GetString("log.file", "engine.log"),
        .flushAlways = config.GetBool("log.flush", true),
    });

    LOG_INFO(Core, "Engine version: ", config.GetString("app.version", "unknown"));
    LOG_INFO(Core, "Content path: ", config.GetString("paths.content", "game/data"));

    engine::core::memory::Memory::InitializeFrameArenas(1024 * 1024);

    // Ticket M00.2 validation sample: arena alloc/reset cycle + tag tracking.
    constexpr int kArenaAllocsPerBatch = 10000;
    void* renderAlloc = engine::core::memory::SystemAllocator::Alloc(4096, alignof(std::max_align_t), engine::core::memory::MemTag::Render);
    void* worldAlloc = engine::core::memory::SystemAllocator::Alloc(2048, alignof(std::max_align_t), engine::core::memory::MemTag::World);

    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([worker]() {
            for (int i = 0; i < 250; ++i) {
                LOG_DEBUG(Job, "worker=", worker, " log=", i);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    double elapsedForReport = 0.0;
    for (int i = 0; i < 180; ++i) {
        engine::core::Time::BeginFrame();

        auto& frameArena = engine::core::memory::Memory::CurrentFrameArena();
        for (int allocIndex = 0; allocIndex < kArenaAllocsPerBatch; ++allocIndex) {
            (void)frameArena.Alloc(32, alignof(std::max_align_t));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        engine::core::Time::EndFrame();
        elapsedForReport += engine::core::Time::DeltaSeconds();

        if (elapsedForReport >= 1.0) {
            LOG_INFO(Core,
                     "Frame=", engine::core::Time::FrameIndex(),
                     " Delta=", engine::core::Time::DeltaSeconds(),
                     " FPS=", engine::core::Time::Fps());
            elapsedForReport = 0.0;
        }
    }

    engine::core::memory::SystemAllocator::Free(renderAlloc, engine::core::memory::MemTag::Render);
    engine::core::memory::SystemAllocator::Free(worldAlloc, engine::core::memory::MemTag::World);
    engine::core::memory::Memory::ShutdownFrameArenas();
    engine::core::memory::Memory::DumpStats();

    engine::core::Log::Shutdown();
    return 0;
}
