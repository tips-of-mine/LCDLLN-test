#include "engine/Engine.h"
#include "engine/core/Config.h"
#include "engine/platform/Window.h"
#include "engine/platform/Input.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/AssetRegistry.h"
#include <cstdio>

int main(int argc, char** argv)
{
    std::fprintf(stderr, "[TEST] Config::Load\n");      std::fflush(stderr);
    auto cfg = engine::core::Config::Load("config.json", argc, argv);
    std::fprintf(stderr, "[TEST] Config OK\n");         std::fflush(stderr);

    std::fprintf(stderr, "[TEST] Window\n");            std::fflush(stderr);
    { engine::platform::Window w; }
    std::fprintf(stderr, "[TEST] Window OK\n");         std::fflush(stderr);

    std::fprintf(stderr, "[TEST] VkInstance\n");        std::fflush(stderr);
    { engine::render::VkInstance v; }
    std::fprintf(stderr, "[TEST] VkInstance OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[TEST] VkDeviceContext\n");   std::fflush(stderr);
    { engine::render::VkDeviceContext v; }
    std::fprintf(stderr, "[TEST] VkDeviceContext OK\n");std::fflush(stderr);

    std::fprintf(stderr, "[TEST] FrameGraph\n");        std::fflush(stderr);
    { engine::render::FrameGraph v; }
    std::fprintf(stderr, "[TEST] FrameGraph OK\n");     std::fflush(stderr);

    std::fprintf(stderr, "[TEST] AssetRegistry\n");     std::fflush(stderr);
    { engine::render::AssetRegistry v; }
    std::fprintf(stderr, "[TEST] AssetRegistry OK\n");  std::fflush(stderr);

    std::fprintf(stderr, "[TEST] Engine()\n");          std::fflush(stderr);
    engine::Engine e(argc, argv);
    return e.Run();
}