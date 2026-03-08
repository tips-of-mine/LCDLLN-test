#include "engine/Engine.h"
#include "engine/platform/Input.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/ShaderCache.h"
#include "engine/world/WorldModel.h"
#include "engine/world/StreamCache.h"
#include "engine/render/GpuUploadQueue.h"
#include "engine/world/ChunkBudgetStats.h"
#include "engine/world/LodConfig.h"
#include "engine/world/HlodRuntime.h"

#include <cstdio>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::unique_ptr<engine::Engine> g_engine;
static int g_result = 1;

static void CreateAndRun(int argc, char** argv)
{
    std::fprintf(stderr, "[T] Input\n");              std::fflush(stderr);
    { engine::platform::Input v; }
    std::fprintf(stderr, "[T] Input OK\n");           std::fflush(stderr);

    std::fprintf(stderr, "[T] VkSwapchain\n");        std::fflush(stderr);
    { engine::render::VkSwapchain v; }
    std::fprintf(stderr, "[T] VkSwapchain OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[T] ShaderCache\n");        std::fflush(stderr);
    { engine::render::ShaderCache v; }
    std::fprintf(stderr, "[T] ShaderCache OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[T] World\n");              std::fflush(stderr);
    { engine::world::World v; }
    std::fprintf(stderr, "[T] World OK\n");           std::fflush(stderr);

    std::fprintf(stderr, "[T] StreamCache\n");        std::fflush(stderr);
    { engine::world::StreamCache v; }
    std::fprintf(stderr, "[T] StreamCache OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[T] GpuUploadQueue\n");     std::fflush(stderr);
    { engine::render::GpuUploadQueue v; }
    std::fprintf(stderr, "[T] GpuUploadQueue OK\n");  std::fflush(stderr);

    std::fprintf(stderr, "[T] ChunkBudgetStats\n");   std::fflush(stderr);
    { engine::world::ChunkBudgetStats v; }
    std::fprintf(stderr, "[T] ChunkBudgetStats OK\n");std::fflush(stderr);

    std::fprintf(stderr, "[T] LodConfig\n");          std::fflush(stderr);
    { engine::world::LodConfig v; }
    std::fprintf(stderr, "[T] LodConfig OK\n");       std::fflush(stderr);

    std::fprintf(stderr, "[T] HlodRuntime\n");        std::fflush(stderr);
    { engine::world::HlodRuntime v; }
    std::fprintf(stderr, "[T] HlodRuntime OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[T] Engine()\n");           std::fflush(stderr);
    g_engine = std::make_unique<engine::Engine>(argc, argv);
    std::fprintf(stderr, "[T] Engine OK\n");          std::fflush(stderr);
    g_result = g_engine->Run();
}

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[MAIN] main() atteint\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[MAIN] avant Engine()\n");
    std::fflush(stderr);

    __try
    {
        CreateAndRun(argc, argv);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        std::fprintf(stderr, "[MAIN] SEH EXCEPTION code=0x%08X\n",
            (unsigned int)GetExceptionCode());
        std::fflush(stderr);
    }
    return g_result;
}