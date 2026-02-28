#include "engine/core/Engine.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Memory.h"
#include "engine/core/MemoryTags.h"
#include "engine/core/Time.h"

#include "engine/platform/FileSystem.h"
#include "engine/platform/Input.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/ShaderCache.h"
#include "engine/render/vk/VkFrameResources.h"
#include "engine/render/vk/VkSceneColor.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/vk/VkGBuffer.h"
#include "engine/render/vk/VkGeometryPipeline.h"
#include "engine/render/vk/VkSceneColorHDR.h"
#include "engine/render/vk/VkLightingPipeline.h"
#include "engine/render/vk/VkTonemapPipeline.h"
#include "engine/render/vk/VkMaterial.h"
#include "engine/render/vk/VkTextureLoader.h"
#include "engine/render/vk/VkShadowMap.h"
#include "engine/render/vk/VkShadowPipeline.h"
#include "engine/render/Csm.h"
#include "engine/render/Halton.h"
#include "engine/math/Frustum.h"
#include "engine/world/HlodRuntime.h"
#include "engine/world/World.h"
#include "engine/streaming/StreamingScheduler.h"
#include "engine/platform/Input.h"

#include <vulkan/vulkan.h>

#include <cstring>

namespace {
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}
} // namespace
#include <cmath>
#include <cstring>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace engine::core;
using namespace engine::platform;
using namespace engine::core::memory;

// ---------------------------------------------------------------------------
// Cube mesh for geometry pass (M03.1, M03.3): position (vec3) + normal (vec3) + UV (vec2)
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kCubeVertexCount = 36;
struct CubeVertex { float px, py, pz, nx, ny, nz, u, v; };
const CubeVertex kCubeVertices[kCubeVertexCount] = {
    // -Z
    {-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f}, { 0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f}, { 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f}, {-0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f}, {-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f},
    // +Z
    {-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f}, { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f}, { 0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f}, {-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f}, {-0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f},
    // -Y
    {-0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f}, { 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f}, { 0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f}, {-0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f}, {-0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f},
    // +Y
    {-0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f}, { 0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f}, { 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f}, {-0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f}, {-0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f},
    // -X
    {-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f}, {-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f}, {-0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f},
    {-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f}, {-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f}, {-0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f},
    // +X
    { 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f}, { 0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f}, { 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f}, { 0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f}, { 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f},
};

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

/**
 * @brief Inverts a 4x4 column-major matrix (for invViewProj).
 */
void Invert4x4ColumnMajor(const float m[16], float out[16]) {
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]   - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]   - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]   + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]   - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]   + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]   + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]   - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]   + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]   - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det == 0.0f) { std::memcpy(out, m, 16 * sizeof(float)); return; }
    det = 1.0f / det;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[c * 4 + r] = inv[r * 4 + c] * det;
}

bool CreateCubeVertexBuffer(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    const VkDeviceSize size = sizeof(kCubeVertices);
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size       = size;
    bci.usage      = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, outBuffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, *outBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(device, *outBuffer, *outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, *outMemory, nullptr);
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device, *outMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(device, *outMemory, nullptr);
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return false;
    }
    std::memcpy(mapped, kCubeVertices, size);
    vkUnmapMemory(device, *outMemory);
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// Engine — public API
// ---------------------------------------------------------------------------

int Engine::Run(int argc, const char* const* argv) {
    // 1) Configuration (must be first so other subsystems can read settings).
    Config::Init("config.json", argc, argv);

    // 2) Logging setup from config.
    const std::string logFile  = Config::GetString("log.file",  "engine.log");
    const std::string levelStr = Config::GetString("log.level", "DEBUG");

    LogLevel logLevel = LogLevel::Debug;
    if      (levelStr == "TRACE")   { logLevel = LogLevel::Trace;   }
    else if (levelStr == "DEBUG")   { logLevel = LogLevel::Debug;   }
    else if (levelStr == "INFO")    { logLevel = LogLevel::Info;    }
    else if (levelStr == "WARNING") { logLevel = LogLevel::Warning; }
    else if (levelStr == "ERROR")   { logLevel = LogLevel::Error;   }

    Log::Init(logFile, logLevel);
    LOG_INFO(Core, "Engine starting — version 0.1.0 (M00.5 Game Loop)");

    // 3) Time subsystem.
    Time::Init(/*maxDelta=*/0.1f, /*fpsWindow=*/120u);
    LOG_INFO(Core, "Time subsystem initialised");

    // 4) FileSystem (content root resolution).
    FileSystem::Init();
    LOG_INFO(Platform, "FileSystem initialised — content root = '{}'",
             FileSystem::ContentRoot());

    // 5) Run the main engine loop.
    Engine engine;
    const int exitCode = engine.RunInternal(argc, argv);

    // 6) Final statistics and shutdown.
    Memory::DumpStats();

    Log::Shutdown();
    Config::Shutdown();

    return exitCode;
}

// ---------------------------------------------------------------------------
// Engine — construction
// ---------------------------------------------------------------------------

Engine::Engine()
    : m_frameArenas(2u * 1024u * 1024u, MemTag::Temp) {
}

// ---------------------------------------------------------------------------
// Engine — main run sequence
// ---------------------------------------------------------------------------

int Engine::RunInternal(int /*argc*/, const char* const* /*argv*/) {
    // Read high-level options from config.
    m_headless        = Config::GetBool ("headless", false);
    m_useFixedTimestep = Config::GetBool("game.fixed_timestep", false);
    m_fixedDeltaSeconds = Config::GetFloat("game.fixed_delta", 1.0f / 60.0f);

    const int  targetFps = Config::GetInt("game.target_fps", 0);
    m_targetFrameTime    = (targetFps > 0) ? (1.0 / static_cast<double>(targetFps))
                                           : 0.0;

    m_vsyncEnabled = Config::GetBool("render.vsync", true);
    LOG_INFO(Core, "Render settings — vsync={} targetFps={}",
             m_vsyncEnabled ? "on" : "off",
             targetFps);

    // M10.3: LRU cache for streaming (1-4GB configurable).
    int cacheMb = Config::GetInt("streaming.cache_size_mb", 2048);
    if (cacheMb < 1024) cacheMb = 1024;
    if (cacheMb > 4096) cacheMb = 4096;
    m_lruCache.SetCapacityBytes(static_cast<size_t>(cacheMb) * 1024u * 1024u);
    m_streamingScheduler.SetCache(&m_lruCache);

    // Window parameters.
    const int  windowWidth  = Config::GetInt   ("window.width",  1280);
    const int  windowHeight = Config::GetInt   ("window.height",  720);
    const std::string title = Config::GetString("window.title",  "MMORPG Engine");

    if (m_headless) {
        LOG_INFO(Platform, "Headless mode: skipping Window/Input creation; running limited headless loop");
    } else {
        // Create the main window.
        m_window.Init(windowWidth, windowHeight, title);
        m_windowOk = true;

        m_framebufferWidth  = m_window.Width();
        m_framebufferHeight = m_window.Height();

        // Register callbacks that forward to Engine hooks.
        m_window.SetResizeCallback([this](int w, int h) {
            OnResize(w, h);
        });
        m_window.SetCloseCallback([this]() {
            OnQuit();
        });

        // Install input handling.
        Input::Install(m_window.NativeHandle());

        // Initialise Vulkan instance + surface for this window.
        const bool vkOk = m_vkInstance.Init(m_window.NativeHandle());
        if (!vkOk) {
            LOG_ERROR(Render, "Vulkan instance initialisation failed; continuing without Vulkan");
        } else {
            // Initialise physical/logical device and queues (M01.2).
            const bool devOk = m_vkDevice.Init(m_vkInstance.Get(), m_vkInstance.Surface());
            if (!devOk) {
                LOG_ERROR(Render, "Vulkan device initialisation failed; continuing without Vulkan device");
            } else {
                const bool swapOk = m_vkSwapchain.Init(
                    m_vkDevice.PhysicalDevice(),
                    m_vkDevice.Device(),
                    m_vkInstance.Surface(),
                    m_vkDevice.Indices(),
                    static_cast<uint32_t>(m_framebufferWidth),
                    static_cast<uint32_t>(m_framebufferHeight));
                if (!swapOk) {
                    LOG_ERROR(Render, "Vulkan swapchain initialisation failed");
                } else {
                    const bool frOk = m_vkFrameResources.Init(
                        m_vkDevice.Device(),
                        m_vkDevice.Indices().graphicsFamily);
                    if (!frOk) {
                        LOG_ERROR(Render, "Vulkan frame resources initialisation failed");
                    } else {
                        int uploadMb = Config::GetInt("streaming.upload_budget_mb", 32);
                        if (uploadMb < 1) uploadMb = 1;
                        if (uploadMb > 128) uploadMb = 128;
                        const VkDeviceSize budgetBytes = static_cast<VkDeviceSize>(uploadMb) * 1024u * 1024u;
                        if (!m_uploadBudget.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), budgetBytes))
                            LOG_ERROR(Render, "VkUploadBudget init failed");
                        if (m_vkSwapchain.IsValid()) {
                        const bool sceneOk = m_vkSceneColor.Init(
                            m_vkDevice.PhysicalDevice(),
                            m_vkDevice.Device(),
                            m_vkSwapchain.Extent().width,
                            m_vkSwapchain.Extent().height,
                            m_vkSwapchain.Format());
                        if (!sceneOk) {
                            LOG_ERROR(Render, "SceneColor offscreen target initialisation failed");
                        } else {
                            const uint32_t w = m_vkSwapchain.Extent().width;
                            const uint32_t h = m_vkSwapchain.Extent().height;
                            const bool taaHistoryOk = m_vkTaaHistory.Init(
                                m_vkDevice.PhysicalDevice(),
                                m_vkDevice.Device(),
                                w, h,
                                m_vkSwapchain.Format());
                            if (!taaHistoryOk) {
                                LOG_ERROR(Render, "TAA history ping-pong initialisation failed");
                            }
                            if (m_vkTaaHistory.IsValid()) {
                                const bool taaOutOk = m_vkTaaOutput.Init(
                                    m_vkDevice.PhysicalDevice(),
                                    m_vkDevice.Device(),
                                    w, h,
                                    m_vkSwapchain.Format());
                                if (!taaOutOk)
                                    LOG_ERROR(Render, "TAA output initialisation failed");
                            }
                            const bool gbufOk = m_vkGBuffer.Init(
                                m_vkDevice.PhysicalDevice(),
                                m_vkDevice.Device(), w, h);
                            if (!gbufOk) {
                                LOG_ERROR(Render, "GBuffer initialisation failed");
                            } else {
                                VkCommandPoolCreateInfo cpci{};
                                cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                                cpci.queueFamilyIndex = m_vkDevice.Indices().graphicsFamily;
                                cpci.flags = 0;
                                if (vkCreateCommandPool(m_vkDevice.Device(), &cpci, nullptr, &m_uploadCommandPool) != VK_SUCCESS) {
                                    LOG_ERROR(Render, "Upload command pool creation failed");
                                } else if (!m_textureLoader.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(),
                                                                 m_vkDevice.GraphicsQueue(), m_uploadCommandPool)) {
                                    LOG_ERROR(Render, "Texture loader initialisation failed");
                                    vkDestroyCommandPool(m_vkDevice.Device(), m_uploadCommandPool, nullptr);
                                    m_uploadCommandPool = VK_NULL_HANDLE;
                                } else {
                                    VkImageView baseView = m_textureLoader.Load("textures/default_basecolor.png", true);
                                    if (baseView == VK_NULL_HANDLE) baseView = m_textureLoader.CreateDefaultTexture(255, 255, 255, true);
                                    VkImageView normView = m_textureLoader.Load("textures/default_normal.png", false);
                                    if (normView == VK_NULL_HANDLE) normView = m_textureLoader.CreateDefaultTexture(128, 128, 255, false);
                                    VkImageView ormView = m_textureLoader.Load("textures/default_orm.png", false);
                                    if (ormView == VK_NULL_HANDLE) ormView = m_textureLoader.CreateDefaultTexture(255, 128, 0, false);
                                    {
                                        float ssaoRadius = Config::GetFloat("ssao.radius", 0.5f);
                                        float ssaoBias   = Config::GetFloat("ssao.bias", 0.01f);
                                        m_ssaoKernelNoise.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(),
                                                              m_vkDevice.GraphicsQueue(), m_uploadCommandPool, ssaoRadius, ssaoBias);
                                    }
                                    if (!engine::render::vk::CreateMaterialDescriptorSetLayout(m_vkDevice.Device(), &m_materialSetLayout)) {
                                        LOG_ERROR(Render, "Material descriptor set layout failed");
                                    } else {
                                        VkDescriptorPoolSize poolSize{};
                                        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                                        poolSize.descriptorCount = 3;
                                        VkDescriptorPoolCreateInfo dpci{};
                                        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                                        dpci.maxSets = 1;
                                        dpci.poolSizeCount = 1;
                                        dpci.pPoolSizes = &poolSize;
                                        if (vkCreateDescriptorPool(m_vkDevice.Device(), &dpci, nullptr, &m_materialDescriptorPool) != VK_SUCCESS) {
                                            vkDestroyDescriptorSetLayout(m_vkDevice.Device(), m_materialSetLayout, nullptr);
                                            m_materialSetLayout = VK_NULL_HANDLE;
                                            LOG_ERROR(Render, "Material descriptor pool failed");
                                        } else {
                                            VkSamplerCreateInfo sci{};
                                            sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                                            sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
                                            sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                                            if (vkCreateSampler(m_vkDevice.Device(), &sci, nullptr, &m_materialSampler) != VK_SUCCESS) {
                                                vkDestroyDescriptorPool(m_vkDevice.Device(), m_materialDescriptorPool, nullptr);
                                                vkDestroyDescriptorSetLayout(m_vkDevice.Device(), m_materialSetLayout, nullptr);
                                                m_materialDescriptorPool = VK_NULL_HANDLE;
                                                m_materialSetLayout = VK_NULL_HANDLE;
                                                LOG_ERROR(Render, "Material sampler failed");
                                            } else {
                                                m_defaultMaterial.baseColorView = baseView;
                                                m_defaultMaterial.normalView = normView;
                                                m_defaultMaterial.ormView = ormView;
                                                m_defaultMaterial.sampler = m_materialSampler;
                                                if (!engine::render::vk::AllocAndUpdateMaterialDescriptorSet(
                                                        m_vkDevice.Device(), m_materialDescriptorPool, m_materialSetLayout,
                                                        m_defaultMaterial, &m_defaultMaterial.descriptorSet)) {
                                                    vkDestroySampler(m_vkDevice.Device(), m_materialSampler, nullptr);
                                                    vkDestroyDescriptorPool(m_vkDevice.Device(), m_materialDescriptorPool, nullptr);
                                                    vkDestroyDescriptorSetLayout(m_vkDevice.Device(), m_materialSetLayout, nullptr);
                                                    m_materialSampler = VK_NULL_HANDLE;
                                                    m_materialDescriptorPool = VK_NULL_HANDLE;
                                                    m_materialSetLayout = VK_NULL_HANDLE;
                                                    LOG_ERROR(Render, "Material descriptor set alloc/update failed");
                                                }
                                            }
                                        }
                                    }
                                }
                                std::vector<uint8_t> vertSpirv = m_shaderCache.Get("shaders/geometry.vert");
                                std::vector<uint8_t> fragSpirv = m_shaderCache.Get("shaders/geometry.frag");
                                if (!vertSpirv.empty() && !fragSpirv.empty() && m_materialSetLayout != VK_NULL_HANDLE) {
                                    if (!m_geometryPipeline.Init(m_vkDevice.Device(),
                                                                 m_vkGBuffer.GetRenderPass(),
                                                                 vertSpirv, fragSpirv, m_materialSetLayout)) {
                                        LOG_ERROR(Render, "Geometry pipeline initialisation failed");
                                    } else if (!CreateCubeVertexBuffer(
                                            m_vkDevice.PhysicalDevice(), m_vkDevice.Device(),
                                            &m_cubeVertexBuffer, &m_cubeVertexBufferMemory)) {
                                        LOG_ERROR(Render, "Cube vertex buffer creation failed");
                                        m_geometryPipeline.Shutdown();
                                    } else {
                                        m_cubeVertexCount = kCubeVertexCount;
                                    }
                                } else {
                                    LOG_ERROR(Render, "Geometry shaders not found or failed to compile");
                                }
                            }
                            if (m_cubeVertexCount > 0) {
                                const bool hdrOk = m_vkSceneColorHDR.Init(
                                    m_vkDevice.PhysicalDevice(),
                                    m_vkDevice.Device(), w, h);
                                if (!hdrOk) {
                                    LOG_ERROR(Render, "SceneColorHDR initialisation failed");
                                } else {
                                    if (m_ssaoKernelNoise.IsValid()) {
                                        if (m_vkSsaoRaw.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), w, h)) {
                                            std::vector<uint8_t> ssaoVert = m_shaderCache.Get("shaders/ssao.vert");
                                            std::vector<uint8_t> ssaoFrag = m_shaderCache.Get("shaders/ssao.frag");
                                            if (!ssaoVert.empty() && !ssaoFrag.empty() &&
                                                m_ssaoPipeline.Init(m_vkDevice.Device(), m_vkSsaoRaw.GetRenderPass(), ssaoVert, ssaoFrag)) {
                                                m_ssaoPipeline.SetInputs(m_vkGBuffer.GetViewDepth(), m_vkGBuffer.GetViewB(),
                                                    m_ssaoKernelNoise.GetKernelBuffer(), m_ssaoKernelNoise.GetKernelBufferSize(),
                                                    m_ssaoKernelNoise.GetNoiseImageView(), m_ssaoKernelNoise.GetNoiseSampler());
                                                if (m_vkSsaoBlur.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), w, h)) {
                                                    std::vector<uint8_t> blurVert = m_shaderCache.Get("shaders/ssao_blur.vert");
                                                    std::vector<uint8_t> blurFrag = m_shaderCache.Get("shaders/ssao_blur.frag");
                                                    if (blurVert.empty() || blurFrag.empty() ||
                                                        !m_ssaoBlurPipeline.Init(m_vkDevice.Device(), m_vkSsaoBlur.GetRenderPass(), blurVert, blurFrag)) {
                                                        if (m_ssaoBlurPipeline.IsValid()) m_ssaoBlurPipeline.Shutdown();
                                                        m_vkSsaoBlur.Shutdown();
                                                    } else {
                                                        m_ssaoBlurPipeline.SetInputs(m_vkSsaoRaw.GetImageView(), m_vkGBuffer.GetViewDepth());
                                                    }
                                                }
                                            } else {
                                                if (m_ssaoPipeline.IsValid()) m_ssaoPipeline.Shutdown();
                                                m_vkSsaoRaw.Shutdown();
                                            }
                                        }
                                    }
                                    std::vector<uint8_t> lvert = m_shaderCache.Get("shaders/lighting.vert");
                                    std::vector<uint8_t> lfrag = m_shaderCache.Get("shaders/lighting.frag");
                                    std::vector<uint8_t> tvert = m_shaderCache.Get("shaders/tonemap.vert");
                                    std::vector<uint8_t> tfrag = m_shaderCache.Get("shaders/tonemap.frag");
                                    if (!lvert.empty() && !lfrag.empty() && !tvert.empty() && !tfrag.empty()) {
                                        if (!m_lightingPipeline.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(),
                                                                     m_vkSceneColorHDR.GetRenderPass(), lvert, lfrag)) {
                                            LOG_ERROR(Render, "Lighting pipeline initialisation failed");
                                        } else {
                                            m_lightingPipeline.SetGBufferViews(
                                                m_vkGBuffer.GetViewA(), m_vkGBuffer.GetViewB(),
                                                m_vkGBuffer.GetViewC(), m_vkGBuffer.GetViewDepth());
                                            m_lightingPipeline.SetSsaoBlurView(m_vkSsaoBlur.IsValid()
                                                ? m_vkSsaoBlur.GetImageViewOutput() : m_defaultMaterial.baseColorView);
                                            if (!m_tonemapPipeline.Init(m_vkDevice.Device(),
                                                                        m_vkSceneColor.GetRenderPass(), tvert, tfrag)) {
                                                LOG_ERROR(Render, "Tonemap pipeline initialisation failed");
                                                m_lightingPipeline.Shutdown();
                                            } else {
                                                m_tonemapPipeline.SetHDRView(m_vkSceneColorHDR.GetImageView());
                                            }
                                            if (m_vkBloomPyramid.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), w, h)) {
                                                std::vector<uint8_t> prefilterVert = m_shaderCache.Get("shaders/bloom_prefilter.vert");
                                                std::vector<uint8_t> prefilterFrag = m_shaderCache.Get("shaders/bloom_prefilter.frag");
                                                std::vector<uint8_t> downVert = m_shaderCache.Get("shaders/bloom_downsample.vert");
                                                std::vector<uint8_t> downFrag = m_shaderCache.Get("shaders/bloom_downsample.frag");
                                                if (!prefilterVert.empty() && !prefilterFrag.empty() && !downVert.empty() && !downFrag.empty()) {
                                                    if (!m_bloomPrefilterPipeline.Init(m_vkDevice.Device(), m_vkBloomPyramid.GetRenderPass(0), prefilterVert, prefilterFrag)) {
                                                        LOG_ERROR(Render, "Bloom prefilter pipeline init failed");
                                                    } else if (!m_bloomDownsamplePipeline.Init(m_vkDevice.Device(), m_vkBloomPyramid.GetRenderPass(1), downVert, downFrag)) {
                                                        LOG_ERROR(Render, "Bloom downsample pipeline init failed");
                                                        m_bloomPrefilterPipeline.Shutdown();
                                                    }
                                                }
                                                if (m_bloomPrefilterPipeline.IsValid()) {
                                                    std::vector<uint8_t> upVert = m_shaderCache.Get("shaders/bloom_upsample.vert");
                                                    std::vector<uint8_t> upFrag = m_shaderCache.Get("shaders/bloom_upsample.frag");
                                                    std::vector<uint8_t> combVert = m_shaderCache.Get("shaders/bloom_combine.vert");
                                                    std::vector<uint8_t> combFrag = m_shaderCache.Get("shaders/bloom_combine.frag");
                                                    if (!upVert.empty() && !upFrag.empty() && !combVert.empty() && !combFrag.empty() &&
                                                        m_vkBloomCombineTarget.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), w, h)) {
                                                        if (!m_bloomUpsamplePipeline.Init(m_vkDevice.Device(), m_vkBloomPyramid.GetUpsampleRenderPass(0), upVert, upFrag)) {
                                                            LOG_ERROR(Render, "Bloom upsample pipeline init failed");
                                                            m_vkBloomCombineTarget.Shutdown();
                                                        } else if (!m_bloomCombinePipeline.Init(m_vkDevice.Device(), m_vkBloomCombineTarget.GetRenderPass(), combVert, combFrag)) {
                                                            LOG_ERROR(Render, "Bloom combine pipeline init failed");
                                                            m_bloomUpsamplePipeline.Shutdown();
                                                            m_vkBloomCombineTarget.Shutdown();
                                                        }
                                                    }
                                                    if (!m_bloomCombinePipeline.IsValid())
                                                        m_vkBloomCombineTarget.Shutdown();
                                                    if (!m_vkBloomCombineTarget.IsValid())
                                                        m_bloomUpsamplePipeline.Shutdown();
                                                }
                                                if (!m_bloomPrefilterPipeline.IsValid())
                                                    m_vkBloomPyramid.Shutdown();
                                            }
                                            {
                                                std::vector<uint8_t> compReduce = m_shaderCache.Get("shaders/luminance_reduce.comp");
                                                std::vector<uint8_t> compMid = m_shaderCache.Get("shaders/luminance_reduce_mid.comp");
                                                std::vector<uint8_t> compFinal = m_shaderCache.Get("shaders/luminance_reduce_final.comp");
                                                if (!compReduce.empty() && !compMid.empty() && !compFinal.empty()) {
                                                    if (!m_vkExposureReduce.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), w * h, compReduce, compMid, compFinal)) {
                                                        LOG_ERROR(Render, "VkExposureReduce init failed");
                                                    }
                                                }
                                                VkBufferCreateInfo ebci{};
                                                ebci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                                                ebci.size = 4u;
                                                ebci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                                                ebci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                                                if (vkCreateBuffer(m_vkDevice.Device(), &ebci, nullptr, &m_exposureFallbackBuffer) == VK_SUCCESS) {
                                                    VkMemoryRequirements memReqs;
                                                    vkGetBufferMemoryRequirements(m_vkDevice.Device(), m_exposureFallbackBuffer, &memReqs);
                                                    VkMemoryAllocateInfo allocInfo{};
                                                    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                                                    allocInfo.allocationSize = memReqs.size;
                                                    allocInfo.memoryTypeIndex = FindMemoryType(m_vkDevice.PhysicalDevice(), memReqs.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                                                    if (allocInfo.memoryTypeIndex != UINT32_MAX &&
                                                        vkAllocateMemory(m_vkDevice.Device(), &allocInfo, nullptr, &m_exposureFallbackMemory) == VK_SUCCESS &&
                                                        vkBindBufferMemory(m_vkDevice.Device(), m_exposureFallbackBuffer, m_exposureFallbackMemory, 0) == VK_SUCCESS) {
                                                        float initExp = 1.0f;
                                                        void* ptr = nullptr;
                                                        if (vkMapMemory(m_vkDevice.Device(), m_exposureFallbackMemory, 0, 4, 0, &ptr) == VK_SUCCESS) {
                                                            std::memcpy(ptr, &initExp, 4);
                                                            vkUnmapMemory(m_vkDevice.Device(), m_exposureFallbackMemory);
                                                        }
                                                        m_tonemapPipeline.SetExposureBuffer(m_exposureFallbackBuffer, 0, 4);
                                                    } else {
                                                        if (m_exposureFallbackMemory != VK_NULL_HANDLE) {
                                                            vkFreeMemory(m_vkDevice.Device(), m_exposureFallbackMemory, nullptr);
                                                            m_exposureFallbackMemory = VK_NULL_HANDLE;
                                                        }
                                                        vkDestroyBuffer(m_vkDevice.Device(), m_exposureFallbackBuffer, nullptr);
                                                        m_exposureFallbackBuffer = VK_NULL_HANDLE;
                                                    }
                                                }
                                            }
                                            {
                                                std::string lutPath = Config::GetString("color_grading.lut_path", "");
                                                VkImageView lutView = VK_NULL_HANDLE;
                                                if (!lutPath.empty())
                                                    lutView = m_textureLoader.Load(lutPath, true);
                                                if (lutView == VK_NULL_HANDLE)
                                                    lutView = m_textureLoader.CreateDefaultTexture(255, 255, 255, true);
                                                if (lutView != VK_NULL_HANDLE)
                                                    m_tonemapPipeline.SetLUTView(lutView);
                                            }
                                            if (m_vkTaaHistory.IsValid() && m_vkTaaOutput.IsValid()) {
                                                std::vector<uint8_t> taaVert = m_shaderCache.Get("shaders/taa.vert");
                                                std::vector<uint8_t> taaFrag = m_shaderCache.Get("shaders/taa.frag");
                                                if (!taaVert.empty() && !taaFrag.empty()) {
                                                    if (!m_taaPipeline.Init(m_vkDevice.Device(),
                                                                          m_vkTaaOutput.GetRenderPass(),
                                                                          taaVert, taaFrag)) {
                                                        LOG_ERROR(Render, "TAA pipeline initialisation failed");
                                                    }
                                                } else {
                                                    LOG_ERROR(Render, "TAA shaders not found or failed to compile");
                                                }
                                            }
                                            m_shadowMapSize = static_cast<uint32_t>(Config::GetInt("render.shadow_map_size", 1024));
                                            if (m_shadowMapSize < 64) m_shadowMapSize = 64;
                                            if (!m_vkShadowMap.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), m_shadowMapSize)) {
                                                LOG_ERROR(Render, "VkShadowMap initialisation failed");
                                            } else {
                                                std::vector<uint8_t> svert = m_shaderCache.Get("shaders/shadow.vert");
                                                std::vector<uint8_t> sfrag = m_shaderCache.Get("shaders/shadow.frag");
                                                if (!svert.empty() && !sfrag.empty()) {
                                                    if (!m_shadowPipeline.Init(m_vkDevice.Device(),
                                                                              m_vkShadowMap.GetRenderPass(), svert, sfrag)) {
                                                        LOG_ERROR(Render, "VkShadowPipeline initialisation failed");
                                                        m_vkShadowMap.Shutdown();
                                                    } else {
                                                        m_lightingPipeline.SetShadowMapViews(
                                                            m_vkShadowMap.GetView(0), m_vkShadowMap.GetView(1),
                                                            m_vkShadowMap.GetView(2), m_vkShadowMap.GetView(3));
                                                    }
                                                } else {
                                                    LOG_ERROR(Render, "Shadow shaders not found or failed to compile");
                                                    m_vkShadowMap.Shutdown();
                                                }
                                            }
                                            std::vector<uint8_t> brdfComp = m_shaderCache.Get("shaders/brdf_lut.comp");
                                            if (!brdfComp.empty()) {
                                                if (m_vkBrdfLut.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), brdfComp)) {
                                                    if (m_vkBrdfLut.Generate(m_vkDevice.Device(), m_vkDevice.GraphicsQueue(),
                                                                             m_vkDevice.Indices().graphicsFamily)) {
                                                        m_lightingPipeline.SetBrdfLutView(m_vkBrdfLut.GetView(), m_vkBrdfLut.GetSampler());
                                                    } else {
                                                        LOG_ERROR(Render, "VkBrdfLut Generate failed");
                                                        m_vkBrdfLut.Shutdown();
                                                    }
                                                } else {
                                                    LOG_ERROR(Render, "VkBrdfLut Init failed");
                                                }
                                            } else {
                                                LOG_WARN(Render, "BRDF LUT compute shader not found or failed to compile");
                                            }
                                            if (m_envCubemapSampler == VK_NULL_HANDLE) {
                                                VkSamplerCreateInfo sciEnv{};
                                                sciEnv.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                                                sciEnv.magFilter = sciEnv.minFilter = VK_FILTER_LINEAR;
                                                sciEnv.addressModeU = sciEnv.addressModeV = sciEnv.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                                                if (vkCreateSampler(m_vkDevice.Device(), &sciEnv, nullptr, &m_envCubemapSampler) != VK_SUCCESS) {
                                                    LOG_ERROR(Render, "Env cubemap sampler failed");
                                                }
                                            }
                                            if (m_envCubemapSampler != VK_NULL_HANDLE) {
                                                std::string envBase = Config::GetString("env.base_path", "env");
                                                VkImageView envView = m_textureLoader.LoadCubemapHDR(envBase);
                                                if (envView != VK_NULL_HANDLE) {
                                                    std::vector<uint8_t> irrComp = m_shaderCache.Get("shaders/irradiance_conv.comp");
                                                    if (!irrComp.empty() && m_vkIrradianceCubemap.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), irrComp, 64)) {
                                                        if (m_vkIrradianceCubemap.Convolve(m_vkDevice.Device(), m_vkDevice.GraphicsQueue(),
                                                                m_vkDevice.Indices().graphicsFamily, envView, m_envCubemapSampler)) {
                                                            m_lightingPipeline.SetIrradianceView(m_vkIrradianceCubemap.GetView(), m_vkIrradianceCubemap.GetSampler());
                                                        } else {
                                                            m_vkIrradianceCubemap.Shutdown();
                                                            m_lightingPipeline.SetIrradianceView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                        }
                                                    } else {
                                                        if (m_vkIrradianceCubemap.IsValid()) m_vkIrradianceCubemap.Shutdown();
                                                        m_lightingPipeline.SetIrradianceView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                    }
                                                    std::vector<uint8_t> prefilterComp = m_shaderCache.Get("shaders/prefilter_specular.comp");
                                                    if (!prefilterComp.empty() && m_vkPrefilteredEnvCubemap.Init(m_vkDevice.PhysicalDevice(), m_vkDevice.Device(), prefilterComp, 256)) {
                                                        if (m_vkPrefilteredEnvCubemap.Prefilter(m_vkDevice.Device(), m_vkDevice.GraphicsQueue(),
                                                                m_vkDevice.Indices().graphicsFamily, envView, m_envCubemapSampler)) {
                                                            m_lightingPipeline.SetPrefilteredEnvView(m_vkPrefilteredEnvCubemap.GetView(), m_vkPrefilteredEnvCubemap.GetSampler());
                                                        } else {
                                                            m_vkPrefilteredEnvCubemap.Shutdown();
                                                            m_lightingPipeline.SetPrefilteredEnvView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                        }
                                                    } else {
                                                        if (m_vkPrefilteredEnvCubemap.IsValid()) m_vkPrefilteredEnvCubemap.Shutdown();
                                                        m_lightingPipeline.SetPrefilteredEnvView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                    }
                                                } else {
                                                    m_lightingPipeline.SetIrradianceView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                    m_lightingPipeline.SetPrefilteredEnvView(m_textureLoader.CreateDefaultCubemap(), m_envCubemapSampler);
                                                }
                                            }
                                        }
                                    } else {
                                        LOG_ERROR(Render, "Lighting/tonemap shaders not found or failed to compile");
                                    }
                                }
                            }
                        }
                    }
                        }
                    }
                }
            }
    }

    m_running        = true;
    m_lastFpsLogTime = 0.0;

    // Main loop.
    if (m_headless) {
        // In headless mode, run a small fixed number of frames for smoke tests.
        constexpr int kMaxHeadlessFrames = 5;
        for (int i = 0; i < kMaxHeadlessFrames && m_running; ++i) {
            BeginFrame();
            Update();
            Render();
            EndFrame();
        }
    } else {
        while (m_running && m_windowOk && !m_window.ShouldClose()) {
            BeginFrame();
            Update();
            Render();
            EndFrame();
        }
    }

    // Shutdown platform subsystems (reverse order of init).
    if (!m_headless && m_windowOk) {
        if (m_cubeVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vkDevice.Device(), m_cubeVertexBuffer, nullptr);
            m_cubeVertexBuffer = VK_NULL_HANDLE;
        }
        if (m_cubeVertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_vkDevice.Device(), m_cubeVertexBufferMemory, nullptr);
            m_cubeVertexBufferMemory = VK_NULL_HANDLE;
        }
        m_tonemapPipeline.Shutdown();
        m_lightingPipeline.Shutdown();
        m_ssaoPipeline.Shutdown();
        m_vkSsaoRaw.Shutdown();
        m_ssaoBlurPipeline.Shutdown();
        m_vkSsaoBlur.Shutdown();
        m_shadowPipeline.Shutdown();
        m_vkShadowMap.Shutdown();
        m_vkBrdfLut.Shutdown();
        m_vkIrradianceCubemap.Shutdown();
        m_vkPrefilteredEnvCubemap.Shutdown();
        m_ssaoKernelNoise.Shutdown();
        if (m_envCubemapSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_vkDevice.Device(), m_envCubemapSampler, nullptr);
            m_envCubemapSampler = VK_NULL_HANDLE;
        }
        m_geometryPipeline.Shutdown();
        if (m_materialSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_vkDevice.Device(), m_materialSampler, nullptr);
            m_materialSampler = VK_NULL_HANDLE;
        }
        if (m_materialDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_vkDevice.Device(), m_materialDescriptorPool, nullptr);
            m_materialDescriptorPool = VK_NULL_HANDLE;
        }
        if (m_materialSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_vkDevice.Device(), m_materialSetLayout, nullptr);
            m_materialSetLayout = VK_NULL_HANDLE;
        }
        m_textureLoader.Shutdown();
        if (m_uploadCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_vkDevice.Device(), m_uploadCommandPool, nullptr);
            m_uploadCommandPool = VK_NULL_HANDLE;
        }
        m_vkSceneColorHDR.Shutdown();
        m_vkGBuffer.Shutdown();
        m_uploadBudget.Shutdown();
        m_vkFrameResources.Shutdown();
        m_vkSceneColor.Shutdown();
        m_taaPipeline.Shutdown();
        m_vkTaaOutput.Shutdown();
        m_vkTaaHistory.Shutdown();
        m_bloomDownsamplePipeline.Shutdown();
        m_bloomPrefilterPipeline.Shutdown();
        m_bloomCombinePipeline.Shutdown();
        m_vkBloomCombineTarget.Shutdown();
        m_bloomUpsamplePipeline.Shutdown();
        m_vkBloomPyramid.Shutdown();
        m_vkExposureReduce.Shutdown();
        if (m_exposureFallbackBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vkDevice.Device(), m_exposureFallbackBuffer, nullptr);
            m_exposureFallbackBuffer = VK_NULL_HANDLE;
        }
        if (m_exposureFallbackMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_vkDevice.Device(), m_exposureFallbackMemory, nullptr);
            m_exposureFallbackMemory = VK_NULL_HANDLE;
        }
        m_vkSwapchain.Shutdown();
        m_vkDevice.Shutdown();
        m_vkInstance.Shutdown();
        Input::Uninstall();
        m_window.Shutdown();
        m_windowOk = false;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Engine — per-frame steps
// ---------------------------------------------------------------------------

void Engine::BeginFrame() {
    // Advance time and reset the appropriate frame arena.
    Time::BeginFrame();
    m_frameArenas.BeginFrame(Time::FrameIndex());

    // M10.3: collect deferred GPU destroys when fences have signaled.
    if (m_vkDevice.IsValid())
        m_deferredDestroyQueue.Collect(m_vkDevice.Device());

    // M10.4: reset upload budget staging for this frame.
    if (m_uploadBudget.IsValid())
        m_uploadBudget.BeginFrame(m_vkFrameResources.CurrentFrameIndex());

    // Pump OS/window events and snapshot input state.
    if (!m_headless && m_windowOk) {
        m_window.PollEvents();
        Input::BeginFrame();

        if (m_window.ShouldClose()) {
            OnQuit();
        }
    }
}

void Engine::Update() {
    using namespace engine::platform;
    using namespace engine::render;
    using namespace engine::math;

    const float dt = m_useFixedTimestep ? m_fixedDeltaSeconds
                                        : Time::DeltaSeconds();

    const std::uint32_t writeIdx = m_renderWriteIndex;
    RenderState& rs = m_renderStates[writeIdx];

    const float w = (m_framebufferWidth  > 0) ? static_cast<float>(m_framebufferWidth)  : 1280.0f;
    const float h = (m_framebufferHeight > 0) ? static_cast<float>(m_framebufferHeight) : 720.0f;
    const float aspect = (h > 0.0f) ? (w / h) : (16.0f / 9.0f);

    m_camera.aspect = aspect;

    CameraControllerInput input;
    input.mouseDeltaX = Input::MouseDeltaX();
    input.mouseDeltaY = Input::MouseDeltaY();
    input.keyW = Input::IsKeyDown(Key::W);
    input.keyA = Input::IsKeyDown(Key::A);
    input.keyS = Input::IsKeyDown(Key::S);
    input.keyD = Input::IsKeyDown(Key::D);
    input.keyShift = Input::IsKeyDown(Key::LeftShift);
    m_cameraController.Update(m_camera, input, dt);

    rs.camera = m_camera;
    ComputeViewMatrix(rs.camera, rs.view.m);
    ComputeProjectionMatrix(rs.camera, rs.proj.m);

    {
        float viewDirX = -std::sin(m_camera.yaw) * std::cos(m_camera.pitch);
        float viewDirZ = -std::cos(m_camera.yaw) * std::cos(m_camera.pitch);
        float len = std::sqrt(viewDirX * viewDirX + viewDirZ * viewDirZ);
        if (len > 1e-6f) { viewDirX /= len; viewDirZ /= len; }
        const double currentTime = static_cast<double>(Time::ElapsedSeconds());
        ::engine::world::EmitChunkRequestsForPosition(
            m_camera.position[0], m_camera.position[2],
            [this, viewDirX, viewDirZ, currentTime](::engine::world::RingType ring, const std::vector<::engine::world::ChunkCoord>& chunks) {
                for (const auto& c : chunks) {
                    for (unsigned at = 0; at < static_cast<unsigned>(::engine::streaming::kChunkMetaSlotCount); ++at)
                        m_streamingScheduler.PushRequest(c, ring, m_camera.position[0], m_camera.position[2], viewDirX, viewDirZ, currentTime, static_cast<::engine::streaming::ChunkAssetType>(at));
                }
            });
    }

    const std::uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
    const bool resetHistory = m_taaResetHistory ||
        (m_framebufferWidth > 0 && m_framebufferHeight > 0 &&
         (std::abs(aspect - m_taaPrevAspect) > 1e-6f || std::abs(rs.camera.fovY - m_taaPrevFov) > 1e-6f));
    if (resetHistory) {
        m_taaPrevAspect = aspect;
        m_taaPrevFov = rs.camera.fovY;
        m_taaCopyHistoryOnReset = true;
    } else {
        const RenderState& prevRs = m_renderStates[readIdx];
        for (int i = 0; i < 16; ++i) rs.viewProjPrev[i] = prevRs.viewProjCurr[i];
        rs.jitterPrev[0] = prevRs.jitterCurr[0];
        rs.jitterPrev[1] = prevRs.jitterCurr[1];
    }

    const uint32_t haltonIndex = m_taaFrameIndex % engine::render::kHaltonSequenceSize;
    float hx, hy;
    engine::render::Halton2D(haltonIndex, hx, hy);
    const float jitterNdcX = (w > 0.0f) ? (2.0f * (hx - 0.5f) / w) : 0.0f;
    const float jitterNdcY = (h > 0.0f) ? (2.0f * (hy - 0.5f) / h) : 0.0f;
    rs.proj.m[12] += jitterNdcX;
    rs.proj.m[13] += jitterNdcY;
    rs.jitterCurr[0] = jitterNdcX;
    rs.jitterCurr[1] = jitterNdcY;

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            rs.viewProjCurr[j * 4 + i] = rs.proj.m[0 * 4 + i] * rs.view.m[j * 4 + 0]
                                        + rs.proj.m[1 * 4 + i] * rs.view.m[j * 4 + 1]
                                        + rs.proj.m[2 * 4 + i] * rs.view.m[j * 4 + 2]
                                        + rs.proj.m[3 * 4 + i] * rs.view.m[j * 4 + 3];
        }
    }
    if (resetHistory) {
        for (int i = 0; i < 16; ++i) rs.viewProjPrev[i] = rs.viewProjCurr[i];
        rs.jitterPrev[0] = rs.jitterCurr[0];
        rs.jitterPrev[1] = rs.jitterCurr[1];
        m_taaResetHistory = false;
    }
    ExtractFromMatrix(rs.viewProjCurr, rs.frustum);
    m_taaFrameIndex++;

    float lightDir[3] = { 0.5f, -1.0f, 0.3f };
    float len = std::sqrt(lightDir[0]*lightDir[0] + lightDir[1]*lightDir[1] + lightDir[2]*lightDir[2]);
    if (len > 1e-6f) {
        lightDir[0] /= len; lightDir[1] /= len; lightDir[2] /= len;
    }
    engine::render::CsmComputeCascades(rs.view.m, rs.camera.nearZ, rs.camera.farZ,
                                       rs.camera.fovY, rs.camera.aspect,
                                       lightDir, m_shadowMapSize, m_csmUniform);

    m_renderReadIndex.store(writeIdx, std::memory_order_release);
    m_renderWriteIndex = writeIdx ^ 1u;
}

void Engine::Render() {
    using namespace engine::render;
    using namespace engine::render::vk;

    // Recreate swapchain on resize; then recreate SceneColor and GBuffer to match (M02.4, M03.1).
    if (m_vkSwapchain.IsValid() && m_vkSwapchain.NeedsRecreate()) {
        const uint32_t w = (m_framebufferWidth  > 0) ? static_cast<uint32_t>(m_framebufferWidth)  : 1u;
        const uint32_t h = (m_framebufferHeight > 0) ? static_cast<uint32_t>(m_framebufferHeight) : 1u;
        if (m_vkSwapchain.Recreate(w, h)) {
            m_vkSceneColor.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
            m_vkGBuffer.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
            m_vkSceneColorHDR.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
            if (m_vkSsaoRaw.IsValid()) {
                m_vkSsaoRaw.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
                if (m_ssaoPipeline.IsValid())
                    m_ssaoPipeline.SetInputs(m_vkGBuffer.GetViewDepth(), m_vkGBuffer.GetViewB(),
                        m_ssaoKernelNoise.GetKernelBuffer(), m_ssaoKernelNoise.GetKernelBufferSize(),
                        m_ssaoKernelNoise.GetNoiseImageView(), m_ssaoKernelNoise.GetNoiseSampler());
            }
            if (m_vkSsaoBlur.IsValid()) {
                m_vkSsaoBlur.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
                m_lightingPipeline.SetSsaoBlurView(m_vkSsaoBlur.GetImageViewOutput());
            }
            if (m_vkTaaHistory.IsValid()) {
                m_vkTaaHistory.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
                m_taaCopyHistoryOnReset = true;
            }
            if (m_vkTaaOutput.IsValid())
                m_vkTaaOutput.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
            if (m_vkBloomPyramid.IsValid())
                m_vkBloomPyramid.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
                if (m_vkBloomCombineTarget.IsValid())
                    m_vkBloomCombineTarget.Recreate(m_vkSwapchain.Extent().width, m_vkSwapchain.Extent().height);
                if (m_vkExposureReduce.IsValid()) {
                    const VkExtent2D ext = m_vkSwapchain.Extent();
                    m_vkExposureReduce.Recreate(ext.width * ext.height);
                }
        }
    }

    if (!m_vkSwapchain.IsValid() || !m_vkFrameResources.IsValid() || !m_vkSceneColor.IsValid() ||
        !m_vkGBuffer.IsValid() || !m_vkSceneColorHDR.IsValid() || !m_geometryPipeline.IsValid() ||
        !m_lightingPipeline.IsValid() || !m_tonemapPipeline.IsValid() || m_cubeVertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    FrameResources& fr = m_vkFrameResources.Current();

    if (vkWaitForFences(m_vkDevice.Device(), 1, &fr.inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkWaitForFences failed");
        return;
    }

    uint32_t imageIndex = 0;
    const VkResult acquireRes = vkAcquireNextImageKHR(
        m_vkDevice.Device(),
        m_vkSwapchain.Get(),
        UINT64_MAX,
        fr.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
        m_vkSwapchain.RequestRecreate();
        return;
    }
    if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR(Render, "vkAcquireNextImageKHR failed (code {})", static_cast<int>(acquireRes));
        return;
    }

    // Build frame graph once: Shadow0..3, Geometry, Lighting, Tonemap, Present (M02.4, M03.1, M03.2, M04.2).
    if (!m_frameGraphBuilt) {
        const VkExtent2D ext = m_vkSwapchain.Extent();
        const uint32_t shadowSize = m_vkShadowMap.IsValid() ? m_vkShadowMap.Size() : 0u;

        if (shadowSize > 0) {
            ImageDesc shadowDesc{};
            shadowDesc.width = shadowSize;
            shadowDesc.height = shadowSize;
            shadowDesc.layers = 1;
            shadowDesc.format = VK_FORMAT_D32_SFLOAT;
            m_fgShadowMap0Id = m_frameGraph.CreateImage(shadowDesc, "ShadowMap0");
            m_fgShadowMap1Id = m_frameGraph.CreateImage(shadowDesc, "ShadowMap1");
            m_fgShadowMap2Id = m_frameGraph.CreateImage(shadowDesc, "ShadowMap2");
            m_fgShadowMap3Id = m_frameGraph.CreateImage(shadowDesc, "ShadowMap3");
        }

        ImageDesc gbufADesc{}; gbufADesc.width = ext.width; gbufADesc.height = ext.height; gbufADesc.layers = 1; gbufADesc.format = VK_FORMAT_R8G8B8A8_SRGB;
        m_fgGBufferAId = m_frameGraph.CreateImage(gbufADesc, "GBufferA");
        ImageDesc gbufBDesc{}; gbufBDesc.width = ext.width; gbufBDesc.height = ext.height; gbufBDesc.layers = 1; gbufBDesc.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        m_fgGBufferBId = m_frameGraph.CreateImage(gbufBDesc, "GBufferB");
        ImageDesc gbufCDesc{}; gbufCDesc.width = ext.width; gbufCDesc.height = ext.height; gbufCDesc.layers = 1; gbufCDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
        m_fgGBufferCId = m_frameGraph.CreateImage(gbufCDesc, "GBufferC");
        ImageDesc gbufVelDesc{}; gbufVelDesc.width = ext.width; gbufVelDesc.height = ext.height; gbufVelDesc.layers = 1; gbufVelDesc.format = VK_FORMAT_R16G16_SFLOAT;
        m_fgGBufferVelocityId = m_frameGraph.CreateImage(gbufVelDesc, "GBufferVelocity");
        ImageDesc depthDesc{}; depthDesc.width = ext.width; depthDesc.height = ext.height; depthDesc.layers = 1; depthDesc.format = VK_FORMAT_D32_SFLOAT;
        m_fgDepthId = m_frameGraph.CreateImage(depthDesc, "Depth");

        ImageDesc sceneHDRDesc{};
        sceneHDRDesc.width  = ext.width;
        sceneHDRDesc.height = ext.height;
        sceneHDRDesc.layers = 1;
        sceneHDRDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        m_fgSceneColorHDRId = m_frameGraph.CreateImage(sceneHDRDesc, "SceneColor_HDR");

        if (m_vkBloomPyramid.IsValid() && m_bloomPrefilterPipeline.IsValid() && m_bloomDownsamplePipeline.IsValid()) {
            for (uint32_t i = 0; i < kBloomMipCount; ++i) {
                ImageDesc bloomDesc{};
                const VkExtent2D be = m_vkBloomPyramid.GetExtent(i);
                bloomDesc.width   = be.width;
                bloomDesc.height = be.height;
                bloomDesc.layers = 1;
                bloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
                m_fgBloomMipId[i] = m_frameGraph.CreateImage(bloomDesc, "BloomMip" + std::to_string(i));
            }
            if (m_vkBloomCombineTarget.IsValid() && m_bloomUpsamplePipeline.IsValid() && m_bloomCombinePipeline.IsValid()) {
                ImageDesc combineDesc{};
                combineDesc.width  = ext.width;
                combineDesc.height = ext.height;
                combineDesc.layers = 1;
                combineDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
                m_fgBloomCombineId = m_frameGraph.CreateImage(combineDesc, "Bloom_Combine");
            }
        }

        ImageDesc ssaoRawDesc{};
        ssaoRawDesc.width  = ext.width;
        ssaoRawDesc.height = ext.height;
        ssaoRawDesc.layers = 1;
        ssaoRawDesc.format = VK_FORMAT_R16_SFLOAT;
        m_fgSsaoRawId = m_frameGraph.CreateImage(ssaoRawDesc, "SSAO_Raw");

        ImageDesc ssaoBlurDesc{};
        ssaoBlurDesc.width  = ext.width;
        ssaoBlurDesc.height = ext.height;
        ssaoBlurDesc.layers = 1;
        ssaoBlurDesc.format = VK_FORMAT_R16_SFLOAT;
        m_fgSsaoBlurTempId = m_frameGraph.CreateImage(ssaoBlurDesc, "SSAO_Blur_temp");
        m_fgSsaoBlurId    = m_frameGraph.CreateImage(ssaoBlurDesc, "SSAO_Blur");

        ImageDesc sceneLDRDesc{};
        sceneLDRDesc.width  = ext.width;
        sceneLDRDesc.height = ext.height;
        sceneLDRDesc.layers = 1;
        sceneLDRDesc.format = m_vkSwapchain.Format();
        m_fgSceneColorId = m_frameGraph.CreateImage(sceneLDRDesc, "SceneColor_LDR");

        if (m_vkTaaHistory.IsValid()) {
            ImageDesc historyDesc{};
            historyDesc.width  = ext.width;
            historyDesc.height = ext.height;
            historyDesc.layers = 1;
            historyDesc.format = m_vkSwapchain.Format();
            m_fgTaaHistoryAId = m_frameGraph.CreateImage(historyDesc, "HistoryA");
            m_fgTaaHistoryBId = m_frameGraph.CreateImage(historyDesc, "HistoryB");
            if (m_vkTaaOutput.IsValid()) {
                ImageDesc taaOutDesc{};
                taaOutDesc.width  = ext.width;
                taaOutDesc.height = ext.height;
                taaOutDesc.layers = 1;
                taaOutDesc.format = m_vkSwapchain.Format();
                m_fgTaaOutputId = m_frameGraph.CreateImage(taaOutDesc, "TAA_Output");
            }
        }

        ImageDesc swapDesc{};
        swapDesc.width  = ext.width;
        swapDesc.height = ext.height;
        swapDesc.layers = 1;
        swapDesc.format = m_vkSwapchain.Format();
        m_fgSwapchainId = m_frameGraph.CreateImage(swapDesc, "Swapchain");

        if (m_vkShadowMap.IsValid() && m_shadowPipeline.IsValid()) {
            for (int c = 0; c < engine::render::kCsmCascadeCount; ++c) {
                const ResourceId shadowId = (c == 0) ? m_fgShadowMap0Id : (c == 1) ? m_fgShadowMap1Id : (c == 2) ? m_fgShadowMap2Id : m_fgShadowMap3Id;
                m_frameGraph.AddPass(std::string("Shadow") + std::to_string(c))
                    .Write(shadowId, ImageUsage::DepthWrite)
                    .Execute([this, c](VkCommandBuffer cmd, Registry&) {
                        VkViewport vp{};
                        vp.x = 0.0f;
                        vp.y = 0.0f;
                        vp.width  = static_cast<float>(m_vkShadowMap.Size());
                        vp.height = static_cast<float>(m_vkShadowMap.Size());
                        vp.minDepth = 0.0f;
                        vp.maxDepth = 1.0f;
                        vkCmdSetViewport(cmd, 0, 1, &vp);
                        VkRect2D scissor{};
                        scissor.offset = {0, 0};
                        scissor.extent = {m_vkShadowMap.Size(), m_vkShadowMap.Size()};
                        vkCmdSetScissor(cmd, 0, 1, &scissor);
                        VkClearValue clearDepth{};
                        clearDepth.depthStencil.depth = 1.0f;
                        clearDepth.depthStencil.stencil = 0;
                        VkRenderPassBeginInfo rpbi{};
                        rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        rpbi.renderPass      = m_vkShadowMap.GetRenderPass();
                        rpbi.framebuffer     = m_vkShadowMap.GetFramebuffer(static_cast<uint32_t>(c));
                        rpbi.renderArea      = {{0, 0}, {m_vkShadowMap.Size(), m_vkShadowMap.Size()}};
                        rpbi.clearValueCount = 1;
                        rpbi.pClearValues    = &clearDepth;
                        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.GetPipeline());
                        vkCmdPushConstants(cmd, m_shadowPipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, 64, m_csmUniform.lightViewProj[c]);
                        VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &m_cubeVertexBuffer, &offset);
                        vkCmdDraw(cmd, m_cubeVertexCount, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                    });
            }
        }

        m_frameGraph.AddPass("Geometry")
            .Write(m_fgGBufferAId, ImageUsage::ColorWrite)
            .Write(m_fgGBufferBId, ImageUsage::ColorWrite)
            .Write(m_fgGBufferCId, ImageUsage::ColorWrite)
            .Write(m_fgGBufferVelocityId, ImageUsage::ColorWrite)
            .Write(m_fgDepthId, ImageUsage::DepthWrite)
            .Execute([this](VkCommandBuffer cmd, Registry&) {
                const std::uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
                const RenderState& rs = m_renderStates[readIdx];
                const float center[3] = { m_currModelMatrix[12], m_currModelMatrix[13], m_currModelMatrix[14] };
                float dx = center[0] - rs.camera.position[0];
                float dy = center[1] - rs.camera.position[1];
                float dz = center[2] - rs.camera.position[2];
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float halfExtent = 2.0f;
                const float aabbMin[3] = { center[0] - halfExtent, center[1] - halfExtent, center[2] - halfExtent };
                const float aabbMax[3] = { center[0] + halfExtent, center[1] + halfExtent, center[2] + halfExtent };
                const bool visible = ::engine::world::VisibleInFrustum(rs.frustum, aabbMin, aabbMax)
                    && ::engine::world::WithinDrawDistance(rs.camera.position, center, rs.camera.farZ);
                const bool useHlod = visible && ::engine::world::UseHlodForDistance(distance);

                float pushData[64];
                std::memcpy(pushData, rs.viewProjCurr, sizeof(rs.viewProjCurr));
                std::memcpy(pushData + 16, rs.viewProjPrev, sizeof(rs.viewProjPrev));
                std::memcpy(pushData + 32, m_currModelMatrix, sizeof(m_currModelMatrix));
                std::memcpy(pushData + 48, m_prevModelMatrix, sizeof(m_prevModelMatrix));
                VkClearValue clearVals[5]{};
                clearVals[0].color.float32[0] = 0.0f; clearVals[0].color.float32[1] = 0.0f; clearVals[0].color.float32[2] = 0.0f; clearVals[0].color.float32[3] = 1.0f;
                clearVals[1].color.float32[0] = 0.0f; clearVals[1].color.float32[1] = 0.0f; clearVals[1].color.float32[2] = 0.0f; clearVals[1].color.float32[3] = 1.0f;
                clearVals[2].color.float32[0] = 0.0f; clearVals[2].color.float32[1] = 0.0f; clearVals[2].color.float32[2] = 0.0f; clearVals[2].color.float32[3] = 1.0f;
                clearVals[3].color.float32[0] = 0.0f; clearVals[3].color.float32[1] = 0.0f; clearVals[3].color.float32[2] = 0.0f; clearVals[3].color.float32[3] = 1.0f;
                clearVals[4].depthStencil.depth = 1.0f; clearVals[4].depthStencil.stencil = 0;
                VkRenderPassBeginInfo rpbi{};
                rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpbi.renderPass      = m_vkGBuffer.GetRenderPass();
                rpbi.framebuffer     = m_vkGBuffer.GetFramebuffer();
                rpbi.renderArea      = {{0, 0}, m_vkGBuffer.Extent()};
                rpbi.clearValueCount = 5;
                rpbi.pClearValues    = clearVals;
                vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkGBuffer.Extent().width); vp.height = static_cast<float>(m_vkGBuffer.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &vp);
                VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkGBuffer.Extent();
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_geometryPipeline.GetPipeline());
                if (m_defaultMaterial.descriptorSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_geometryPipeline.GetPipelineLayout(),
                                           0, 1, &m_defaultMaterial.descriptorSet, 0, nullptr);
                }
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &m_cubeVertexBuffer, &offset);
                vkCmdPushConstants(cmd, m_geometryPipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, 256, pushData);
                if (visible) {
                    vkCmdDraw(cmd, m_cubeVertexCount, 1, 0, 0);
                    if (useHlod)
                        ++m_hlodDrawsThisFrame;
                    else
                        ++m_instanceDrawsThisFrame;
                    m_chunkStats.RecordDraw(
                        ::engine::world::ChunkCoord{0, 0, 0, 0},
                        ::engine::world::RingType::Active,
                        1u,
                        m_cubeVertexCount / 3u);
                }
                vkCmdEndRenderPass(cmd);
            });

        if (m_vkSsaoRaw.IsValid() && m_ssaoPipeline.IsValid()) {
            m_frameGraph.AddPass("SSAO_Generate")
                .Read(m_fgDepthId, ImageUsage::SampledRead)
                .Read(m_fgGBufferBId, ImageUsage::SampledRead)
                .Write(m_fgSsaoRawId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    const std::uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
                    const RenderState& rs = m_renderStates[readIdx];
                    float invProj[16];
                    Invert4x4ColumnMajor(rs.proj.m, invProj);
                    float pushData[48];
                    std::memcpy(pushData, invProj, 64);
                    std::memcpy(pushData + 16, rs.proj.m, 64);
                    std::memcpy(pushData + 32, rs.view.m, 64);
                    vkCmdPushConstants(cmd, m_ssaoPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 192, pushData);
                    VkClearValue clearVal{}; clearVal.color.float32[0] = 1.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkSsaoRaw.GetRenderPass();
                    rpbi.framebuffer     = m_vkSsaoRaw.GetFramebuffer();
                    rpbi.renderArea      = {{0, 0}, m_vkSsaoRaw.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearVal;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSsaoRaw.Extent().width); vp.height = static_cast<float>(m_vkSsaoRaw.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSsaoRaw.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline.GetPipelineLayout(), 0, 1, &m_ssaoPipeline.GetDescriptorSet(), 0, nullptr);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
        }

        if (m_vkSsaoBlur.IsValid() && m_ssaoBlurPipeline.IsValid()) {
            m_frameGraph.AddPass("SSAO_Blur_H")
                .Read(m_fgSsaoRawId, ImageUsage::SampledRead)
                .Read(m_fgDepthId, ImageUsage::SampledRead)
                .Write(m_fgSsaoBlurTempId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    m_ssaoBlurPipeline.SetInputs(m_vkSsaoRaw.GetImageView(), m_vkGBuffer.GetViewDepth());
                    float invW = 1.0f / static_cast<float>(m_vkSsaoBlur.Extent().width);
                    float invH = 1.0f / static_cast<float>(m_vkSsaoBlur.Extent().height);
                    float pushData[4] = { 1.0f, 0.0f, invW, invH };
                    vkCmdPushConstants(cmd, m_ssaoBlurPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pushData);
                    VkClearValue clearVal{}; clearVal.color.float32[0] = 1.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkSsaoBlur.GetRenderPass();
                    rpbi.framebuffer     = m_vkSsaoBlur.GetFramebufferTemp();
                    rpbi.renderArea      = {{0, 0}, m_vkSsaoBlur.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearVal;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSsaoBlur.Extent().width); vp.height = static_cast<float>(m_vkSsaoBlur.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSsaoBlur.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline.GetPipelineLayout(), 0, 1, &m_ssaoBlurPipeline.GetDescriptorSet(), 0, nullptr);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
            m_frameGraph.AddPass("SSAO_Blur_V")
                .Read(m_fgSsaoBlurTempId, ImageUsage::SampledRead)
                .Read(m_fgDepthId, ImageUsage::SampledRead)
                .Write(m_fgSsaoBlurId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    m_ssaoBlurPipeline.SetInputs(m_vkSsaoBlur.GetImageViewTemp(), m_vkGBuffer.GetViewDepth());
                    float invW = 1.0f / static_cast<float>(m_vkSsaoBlur.Extent().width);
                    float invH = 1.0f / static_cast<float>(m_vkSsaoBlur.Extent().height);
                    float pushData[4] = { 0.0f, 1.0f, invW, invH };
                    vkCmdPushConstants(cmd, m_ssaoBlurPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pushData);
                    VkClearValue clearVal{}; clearVal.color.float32[0] = 1.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkSsaoBlur.GetRenderPass();
                    rpbi.framebuffer     = m_vkSsaoBlur.GetFramebufferOutput();
                    rpbi.renderArea      = {{0, 0}, m_vkSsaoBlur.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearVal;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSsaoBlur.Extent().width); vp.height = static_cast<float>(m_vkSsaoBlur.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSsaoBlur.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoBlurPipeline.GetPipelineLayout(), 0, 1, &m_ssaoBlurPipeline.GetDescriptorSet(), 0, nullptr);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
        }

        {
            auto lightingPass = m_frameGraph.AddPass("Lighting");
            lightingPass.Read(m_fgGBufferAId, ImageUsage::SampledRead)
                .Read(m_fgGBufferBId, ImageUsage::SampledRead)
                .Read(m_fgGBufferCId, ImageUsage::SampledRead)
                .Read(m_fgDepthId, ImageUsage::SampledRead);
            if (m_vkSsaoBlur.IsValid())
                lightingPass.Read(m_fgSsaoBlurId, ImageUsage::SampledRead);
            if (m_vkShadowMap.IsValid()) {
                lightingPass.Read(m_fgShadowMap0Id, ImageUsage::SampledRead)
                    .Read(m_fgShadowMap1Id, ImageUsage::SampledRead)
                    .Read(m_fgShadowMap2Id, ImageUsage::SampledRead)
                    .Read(m_fgShadowMap3Id, ImageUsage::SampledRead);
            }
            lightingPass.Write(m_fgSceneColorHDRId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                const float shadowBiasConst = static_cast<float>(Config::GetFloat("shadow.bias_constant", 0.002));
                const float shadowBiasSlope = static_cast<float>(Config::GetFloat("shadow.bias_slope", 0.5f));
                const std::uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
                const RenderState& rs = m_renderStates[readIdx];
                float viewProj[16];
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        viewProj[j * 4 + i] = rs.proj.m[0 * 4 + i] * rs.view.m[j * 4 + 0]
                                            + rs.proj.m[1 * 4 + i] * rs.view.m[j * 4 + 1]
                                            + rs.proj.m[2 * 4 + i] * rs.view.m[j * 4 + 2]
                                            + rs.proj.m[3 * 4 + i] * rs.view.m[j * 4 + 3];
                    }
                }
                float invViewProj[16];
                Invert4x4ColumnMajor(viewProj, invViewProj);
                const float lightDir[3] = { 0.5f, -1.0f, 0.3f };
                const float lightColor[3] = { 1.0f, 0.95f, 0.9f };
                const float ambient[3] = { 0.05f, 0.05f, 0.08f };
                const engine::render::CsmUniform* csmPtr = m_vkShadowMap.IsValid() ? &m_csmUniform : nullptr;
                m_lightingPipeline.UpdateUniforms(invViewProj, rs.view.m, rs.camera.position, lightDir, lightColor, ambient,
                                                csmPtr, shadowBiasConst, shadowBiasSlope, m_shadowMapSize);
                VkClearValue clearHDR{}; clearHDR.color.float32[0] = 0.0f; clearHDR.color.float32[1] = 0.0f; clearHDR.color.float32[2] = 0.0f; clearHDR.color.float32[3] = 1.0f;
                VkRenderPassBeginInfo rpbi{};
                rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpbi.renderPass      = m_vkSceneColorHDR.GetRenderPass();
                rpbi.framebuffer     = m_vkSceneColorHDR.GetFramebuffer();
                rpbi.renderArea      = {{0, 0}, m_vkSceneColorHDR.Extent()};
                rpbi.clearValueCount = 1;
                rpbi.pClearValues    = &clearHDR;
                vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSceneColorHDR.Extent().width); vp.height = static_cast<float>(m_vkSceneColorHDR.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &vp);
                VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSceneColorHDR.Extent();
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline.GetPipeline());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline.GetPipelineLayout(), 0, 1, &m_lightingPipeline.GetDescriptorSet(), 0, nullptr);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            });
        }

        if (m_vkBloomPyramid.IsValid() && m_bloomPrefilterPipeline.IsValid() && m_bloomDownsamplePipeline.IsValid()) {
            m_frameGraph.AddPass("Bloom_Prefilter")
                .Read(m_fgSceneColorHDRId, ImageUsage::SampledRead)
                .Write(m_fgBloomMipId[0], ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    m_bloomPrefilterPipeline.SetHDRView(m_vkSceneColorHDR.GetImageView());
                    const float threshold = static_cast<float>(Config::GetFloat("bloom.threshold", 1.0));
                    const float knee = static_cast<float>(Config::GetFloat("bloom.knee", 0.5));
                    float pushData[2] = { threshold, knee };
                    vkCmdPushConstants(cmd, m_bloomPrefilterPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, pushData);
                    VkClearValue clearVal{}; clearVal.color.float32[0] = 0.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass = m_vkBloomPyramid.GetRenderPass(0);
                    rpbi.framebuffer = m_vkBloomPyramid.GetFramebuffer(0);
                    rpbi.renderArea = {{0, 0}, m_vkBloomPyramid.GetExtent(0)};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues = &clearVal;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkBloomPyramid.GetExtent(0).width); vp.height = static_cast<float>(m_vkBloomPyramid.GetExtent(0).height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkBloomPyramid.GetExtent(0);
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPrefilterPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPrefilterPipeline.GetPipelineLayout(), 0, 1, &m_bloomPrefilterPipeline.GetDescriptorSet(), 0, nullptr);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
            for (uint32_t d = 0; d < kBloomMipCount - 1u; ++d) {
                const uint32_t srcLev = d;
                const uint32_t dstLev = d + 1u;
                const ResourceId srcId = m_fgBloomMipId[srcLev];
                const ResourceId dstId = m_fgBloomMipId[dstLev];
                m_frameGraph.AddPass("Bloom_Downsample_" + std::to_string(dstLev))
                    .Read(srcId, ImageUsage::SampledRead)
                    .Write(dstId, ImageUsage::ColorWrite)
                    .Execute([this, srcLev, dstLev](VkCommandBuffer cmd, Registry&) {
                        m_bloomDownsamplePipeline.SetSourceView(m_vkBloomPyramid.GetView(srcLev));
                        const VkExtent2D srcExt = m_vkBloomPyramid.GetExtent(srcLev);
                        float invW = 1.0f / static_cast<float>(srcExt.width);
                        float invH = 1.0f / static_cast<float>(srcExt.height);
                        float pushData[2] = { invW, invH };
                        vkCmdPushConstants(cmd, m_bloomDownsamplePipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, pushData);
                        VkClearValue clearVal{}; clearVal.color.float32[0] = 0.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                        VkRenderPassBeginInfo rpbi{};
                        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        rpbi.renderPass = m_vkBloomPyramid.GetRenderPass(dstLev);
                        rpbi.framebuffer = m_vkBloomPyramid.GetFramebuffer(dstLev);
                        rpbi.renderArea = {{0, 0}, m_vkBloomPyramid.GetExtent(dstLev)};
                        rpbi.clearValueCount = 1;
                        rpbi.pClearValues = &clearVal;
                        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                        VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkBloomPyramid.GetExtent(dstLev).width); vp.height = static_cast<float>(m_vkBloomPyramid.GetExtent(dstLev).height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                        vkCmdSetViewport(cmd, 0, 1, &vp);
                        VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkBloomPyramid.GetExtent(dstLev);
                        vkCmdSetScissor(cmd, 0, 1, &scissor);
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownsamplePipeline.GetPipeline());
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownsamplePipeline.GetPipelineLayout(), 0, 1, &m_bloomDownsamplePipeline.GetDescriptorSet(), 0, nullptr);
                        vkCmdDraw(cmd, 3, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                    });
            }
            if (m_vkBloomCombineTarget.IsValid() && m_bloomUpsamplePipeline.IsValid() && m_bloomCombinePipeline.IsValid()) {
                for (uint32_t u = 0; u < kBloomMipCount - 1u; ++u) {
                    const uint32_t srcLev = kBloomMipCount - 1u - u;
                    const uint32_t dstLev = srcLev - 1u;
                    const ResourceId srcId = m_fgBloomMipId[srcLev];
                    const ResourceId dstId = m_fgBloomMipId[dstLev];
                    m_frameGraph.AddPass("Bloom_Upsample_" + std::to_string(dstLev))
                        .Read(srcId, ImageUsage::SampledRead)
                        .Write(dstId, ImageUsage::ColorWrite)
                        .Execute([this, srcLev, dstLev](VkCommandBuffer cmd, Registry&) {
                            m_bloomUpsamplePipeline.SetSourceView(m_vkBloomPyramid.GetView(srcLev));
                            VkRenderPassBeginInfo rpbi{};
                            rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                            rpbi.renderPass = m_vkBloomPyramid.GetUpsampleRenderPass(dstLev);
                            rpbi.framebuffer = m_vkBloomPyramid.GetUpsampleFramebuffer(dstLev);
                            rpbi.renderArea = {{0, 0}, m_vkBloomPyramid.GetExtent(dstLev)};
                            rpbi.clearValueCount = 0;
                            rpbi.pClearValues = nullptr;
                            vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                            VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkBloomPyramid.GetExtent(dstLev).width); vp.height = static_cast<float>(m_vkBloomPyramid.GetExtent(dstLev).height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                            vkCmdSetViewport(cmd, 0, 1, &vp);
                            VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkBloomPyramid.GetExtent(dstLev);
                            vkCmdSetScissor(cmd, 0, 1, &scissor);
                            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomUpsamplePipeline.GetPipeline());
                            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomUpsamplePipeline.GetPipelineLayout(), 0, 1, &m_bloomUpsamplePipeline.GetDescriptorSet(), 0, nullptr);
                            vkCmdDraw(cmd, 3, 1, 0, 0);
                            vkCmdEndRenderPass(cmd);
                        });
                }
                m_frameGraph.AddPass("Bloom_Combine")
                    .Read(m_fgSceneColorHDRId, ImageUsage::SampledRead)
                    .Read(m_fgBloomMipId[0], ImageUsage::SampledRead)
                    .Write(m_fgBloomCombineId, ImageUsage::ColorWrite)
                    .Execute([this](VkCommandBuffer cmd, Registry&) {
                        m_bloomCombinePipeline.SetInputs(m_vkSceneColorHDR.GetImageView(), m_vkBloomPyramid.GetView(0));
                        const float intensity = static_cast<float>(Config::GetFloat("bloom.intensity", 0.3));
                        vkCmdPushConstants(cmd, m_bloomCombinePipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &intensity);
                        VkClearValue clearVal{}; clearVal.color.float32[0] = 0.0f; clearVal.color.float32[1] = 0.0f; clearVal.color.float32[2] = 0.0f; clearVal.color.float32[3] = 1.0f;
                        VkRenderPassBeginInfo rpbi{};
                        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        rpbi.renderPass = m_vkBloomCombineTarget.GetRenderPass();
                        rpbi.framebuffer = m_vkBloomCombineTarget.GetFramebuffer();
                        rpbi.renderArea = {{0, 0}, m_vkBloomCombineTarget.Extent()};
                        rpbi.clearValueCount = 1;
                        rpbi.pClearValues = &clearVal;
                        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                        VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkBloomCombineTarget.Extent().width); vp.height = static_cast<float>(m_vkBloomCombineTarget.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                        vkCmdSetViewport(cmd, 0, 1, &vp);
                        VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkBloomCombineTarget.Extent();
                        vkCmdSetScissor(cmd, 0, 1, &scissor);
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomCombinePipeline.GetPipeline());
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomCombinePipeline.GetPipelineLayout(), 0, 1, &m_bloomCombinePipeline.GetDescriptorSet(), 0, nullptr);
                        vkCmdDraw(cmd, 3, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                    });
            }
        }

        const bool bloomCombineActive = m_fgBloomCombineId != ::engine::render::kInvalidResourceId;
        const bool autoExposureActive = m_vkExposureReduce.IsValid();
        if (bloomCombineActive && autoExposureActive) {
            m_frameGraph.AddPass("Exposure_Reduce")
                .Read(m_fgBloomCombineId, ImageUsage::SampledRead)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    m_vkExposureReduce.SetHDRView(m_vkBloomCombineTarget.GetImageView());
                    const VkExtent2D ext = m_vkBloomCombineTarget.Extent();
                    const float dt = static_cast<float>(Time::DeltaSeconds());
                    const float key = static_cast<float>(Config::GetFloat("exposure.key", 0.18));
                    const float speed = static_cast<float>(Config::GetFloat("exposure.speed", 1.0));
                    m_vkExposureReduce.Dispatch(cmd, ext.width, ext.height, dt, key, speed);
                });
        } else if (!bloomCombineActive && autoExposureActive) {
            m_frameGraph.AddPass("Exposure_Reduce")
                .Read(m_fgSceneColorHDRId, ImageUsage::SampledRead)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    m_vkExposureReduce.SetHDRView(m_vkSceneColorHDR.GetImageView());
                    const VkExtent2D ext = m_vkSceneColorHDR.Extent();
                    const float dt = static_cast<float>(Time::DeltaSeconds());
                    const float key = static_cast<float>(Config::GetFloat("exposure.key", 0.18));
                    const float speed = static_cast<float>(Config::GetFloat("exposure.speed", 1.0));
                    m_vkExposureReduce.Dispatch(cmd, ext.width, ext.height, dt, key, speed);
                });
        }
        if (bloomCombineActive) {
            m_frameGraph.AddPass("Tonemap")
                .Read(m_fgBloomCombineId, ImageUsage::SampledRead)
                .Write(m_fgSceneColorId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    if (autoExposureActive) {
                        m_tonemapPipeline.SetExposureBuffer(m_vkExposureReduce.GetExposureBuffer(),
                            m_vkExposureReduce.GetExposureBufferOffset(), m_vkExposureReduce.GetExposureBufferSize());
                    } else if (m_exposureFallbackBuffer != VK_NULL_HANDLE) {
                        float manualExp = static_cast<float>(Config::GetFloat("tonemap.exposure", 1.0));
                        void* ptr = nullptr;
                        if (vkMapMemory(m_vkDevice.Device(), m_exposureFallbackMemory, 0, 4, 0, &ptr) == VK_SUCCESS) {
                            std::memcpy(ptr, &manualExp, 4);
                            vkUnmapMemory(m_vkDevice.Device(), m_exposureFallbackMemory);
                        }
                        m_tonemapPipeline.SetExposureBuffer(m_exposureFallbackBuffer, 0, 4);
                    }
                    m_tonemapPipeline.SetHDRView(m_vkBloomCombineTarget.GetImageView());
                    VkClearValue clearColor{}; clearColor.color.float32[0] = 0.1f; clearColor.color.float32[1] = 0.1f; clearColor.color.float32[2] = 0.15f; clearColor.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkSceneColor.GetRenderPass();
                    rpbi.framebuffer     = m_vkSceneColor.GetFramebuffer();
                    rpbi.renderArea      = {{0, 0}, m_vkSceneColor.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearColor;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSceneColor.Extent().width); vp.height = static_cast<float>(m_vkSceneColor.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSceneColor.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline.GetPipelineLayout(), 0, 1, &m_tonemapPipeline.GetDescriptorSet(), 0, nullptr);
                    struct { uint32_t lutEnable; float lutStrength; } lutParams;
                    lutParams.lutEnable = (Config::GetInt("color_grading.enable", 0) != 0) ? 1u : 0u;
                    lutParams.lutStrength = static_cast<float>(Config::GetFloat("color_grading.strength", 0.0));
                    vkCmdPushConstants(cmd, m_tonemapPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, &lutParams);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
        } else {
            m_frameGraph.AddPass("Tonemap")
                .Read(m_fgSceneColorHDRId, ImageUsage::SampledRead)
                .Write(m_fgSceneColorId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    if (autoExposureActive) {
                        m_tonemapPipeline.SetExposureBuffer(m_vkExposureReduce.GetExposureBuffer(),
                            m_vkExposureReduce.GetExposureBufferOffset(), m_vkExposureReduce.GetExposureBufferSize());
                    } else if (m_exposureFallbackBuffer != VK_NULL_HANDLE) {
                        float manualExp = static_cast<float>(Config::GetFloat("tonemap.exposure", 1.0));
                        void* ptr = nullptr;
                        if (vkMapMemory(m_vkDevice.Device(), m_exposureFallbackMemory, 0, 4, 0, &ptr) == VK_SUCCESS) {
                            std::memcpy(ptr, &manualExp, 4);
                            vkUnmapMemory(m_vkDevice.Device(), m_exposureFallbackMemory);
                        }
                        m_tonemapPipeline.SetExposureBuffer(m_exposureFallbackBuffer, 0, 4);
                    }
                    m_tonemapPipeline.SetHDRView(m_vkSceneColorHDR.GetImageView());
                    VkClearValue clearColor{}; clearColor.color.float32[0] = 0.1f; clearColor.color.float32[1] = 0.1f; clearColor.color.float32[2] = 0.15f; clearColor.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkSceneColor.GetRenderPass();
                    rpbi.framebuffer     = m_vkSceneColor.GetFramebuffer();
                    rpbi.renderArea      = {{0, 0}, m_vkSceneColor.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearColor;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkSceneColor.Extent().width); vp.height = static_cast<float>(m_vkSceneColor.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkSceneColor.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline.GetPipelineLayout(), 0, 1, &m_tonemapPipeline.GetDescriptorSet(), 0, nullptr);
                    struct { uint32_t lutEnable; float lutStrength; } lutParams;
                    lutParams.lutEnable = (Config::GetInt("color_grading.enable", 0) != 0) ? 1u : 0u;
                    lutParams.lutStrength = static_cast<float>(Config::GetFloat("color_grading.strength", 0.0));
                    vkCmdPushConstants(cmd, m_tonemapPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, &lutParams);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
        }

        if (m_vkTaaHistory.IsValid()) {
            m_frameGraph.AddPass("TAA_CopyToHistoryA")
                .Read(m_fgSceneColorId, ImageUsage::TransferSrc)
                .Write(m_fgTaaHistoryAId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    if (!m_vkTaaHistory.IsValid()) return;
                    if (m_taaFirstFrame || (m_taaCopyHistoryOnReset && m_taaHistoryIdx == 0u)) {
                        const VkImage srcImg = reg.GetImage(m_fgSceneColorId);
                        const VkImage dstImg = reg.GetImage(m_fgTaaHistoryAId);
                        if (srcImg != VK_NULL_HANDLE && dstImg != VK_NULL_HANDLE) {
                            const VkExtent2D ext = m_vkTaaHistory.Extent();
                            VkImageCopy region{};
                            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            region.extent = {ext.width, ext.height, 1};
                            vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                        }
                        if (m_taaCopyHistoryOnReset && m_taaHistoryIdx == 0u)
                            m_taaCopyHistoryOnReset = false;
                    }
                });
            m_frameGraph.AddPass("TAA_CopyToHistoryB")
                .Read(m_fgSceneColorId, ImageUsage::TransferSrc)
                .Write(m_fgTaaHistoryBId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    if (!m_vkTaaHistory.IsValid()) return;
                    if (m_taaFirstFrame || (m_taaCopyHistoryOnReset && m_taaHistoryIdx == 1u)) {
                        const VkImage srcImg = reg.GetImage(m_fgSceneColorId);
                        const VkImage dstImg = reg.GetImage(m_fgTaaHistoryBId);
                        if (srcImg != VK_NULL_HANDLE && dstImg != VK_NULL_HANDLE) {
                            const VkExtent2D ext = m_vkTaaHistory.Extent();
                            VkImageCopy region{};
                            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                            region.extent = {ext.width, ext.height, 1};
                            vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                        }
                        if (m_taaFirstFrame)
                            m_taaFirstFrame = false;
                        if (m_taaCopyHistoryOnReset && m_taaHistoryIdx == 1u)
                            m_taaCopyHistoryOnReset = false;
                    }
                });
        }

        const bool taaPassActive = m_taaPipeline.IsValid() && m_fgTaaOutputId != ::engine::render::kInvalidResourceId;
        if (taaPassActive) {
            m_frameGraph.AddPass("TAA")
                .Read(m_fgSceneColorId, ImageUsage::SampledRead)
                .Read(m_fgTaaHistoryAId, ImageUsage::SampledRead)
                .Read(m_fgTaaHistoryBId, ImageUsage::SampledRead)
                .Read(m_fgGBufferVelocityId, ImageUsage::SampledRead)
                .Read(m_fgDepthId, ImageUsage::SampledRead)
                .Write(m_fgTaaOutputId, ImageUsage::ColorWrite)
                .Execute([this](VkCommandBuffer cmd, Registry&) {
                    if (!m_taaPipeline.IsValid() || !m_vkTaaOutput.IsValid()) return;
                    m_taaPipeline.SetInputs(
                        m_vkSceneColor.GetImageView(),
                        m_vkTaaHistory.GetView(0),
                        m_vkTaaHistory.GetView(1),
                        m_vkGBuffer.GetViewD(),
                        m_vkGBuffer.GetViewDepth());
                    const uint32_t prevIdx = m_taaHistoryIdx ^ 1u;
                    vkCmdPushConstants(cmd, m_taaPipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &prevIdx);
                    VkClearValue clearColor{}; clearColor.color.float32[0] = 0.0f; clearColor.color.float32[1] = 0.0f; clearColor.color.float32[2] = 0.0f; clearColor.color.float32[3] = 1.0f;
                    VkRenderPassBeginInfo rpbi{};
                    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    rpbi.renderPass      = m_vkTaaOutput.GetRenderPass();
                    rpbi.framebuffer     = m_vkTaaOutput.GetFramebuffer();
                    rpbi.renderArea      = {{0, 0}, m_vkTaaOutput.Extent()};
                    rpbi.clearValueCount = 1;
                    rpbi.pClearValues    = &clearColor;
                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp{}; vp.x = 0; vp.y = 0; vp.width = static_cast<float>(m_vkTaaOutput.Extent().width); vp.height = static_cast<float>(m_vkTaaOutput.Extent().height); vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = m_vkTaaOutput.Extent();
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taaPipeline.GetPipeline());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taaPipeline.GetPipelineLayout(), 0, 1, &m_taaPipeline.GetDescriptorSet(), 0, nullptr);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                });
            m_frameGraph.AddPass("TAA_CopyToA")
                .Read(m_fgTaaOutputId, ImageUsage::TransferSrc)
                .Write(m_fgTaaHistoryAId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    if (m_taaHistoryIdx != 0u) return;
                    const VkImage srcImg = reg.GetImage(m_fgTaaOutputId);
                    const VkImage dstImg = reg.GetImage(m_fgTaaHistoryAId);
                    if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
                    const VkExtent2D ext = m_vkTaaOutput.Extent();
                    VkImageCopy region{};
                    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.extent = {ext.width, ext.height, 1};
                    vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                });
            m_frameGraph.AddPass("TAA_CopyToB")
                .Read(m_fgTaaOutputId, ImageUsage::TransferSrc)
                .Write(m_fgTaaHistoryBId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    if (m_taaHistoryIdx != 1u) return;
                    const VkImage srcImg = reg.GetImage(m_fgTaaOutputId);
                    const VkImage dstImg = reg.GetImage(m_fgTaaHistoryBId);
                    if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
                    const VkExtent2D ext = m_vkTaaOutput.Extent();
                    VkImageCopy region{};
                    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.extent = {ext.width, ext.height, 1};
                    vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                });
            m_frameGraph.AddPass("Present")
                .Read(m_fgTaaOutputId, ImageUsage::TransferSrc)
                .Write(m_fgSwapchainId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    const VkImage srcImg = reg.GetImage(m_fgTaaOutputId);
                    const VkImage dstImg = reg.GetImage(m_fgSwapchainId);
                    if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
                    const VkExtent2D ext = m_vkTaaOutput.Extent();
                    VkImageCopy region{};
                    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.extent = {ext.width, ext.height, 1};
                    vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    VkImageMemoryBarrier bar{};
                    bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    bar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    bar.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.image               = dstImg;
                    bar.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    bar.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                    bar.dstAccessMask       = 0;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
                });
        } else {
            m_frameGraph.AddPass("Present")
                .Read(m_fgSceneColorId, ImageUsage::TransferSrc)
                .Write(m_fgSwapchainId, ImageUsage::TransferDst)
                .Execute([this](VkCommandBuffer cmd, Registry& reg) {
                    const VkImage srcImg = reg.GetImage(m_fgSceneColorId);
                    const VkImage dstImg = reg.GetImage(m_fgSwapchainId);
                    if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
                    const VkExtent2D ext = m_vkSceneColor.Extent();
                    VkImageCopy region{};
                    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.extent         = {ext.width, ext.height, 1};
                    vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    VkImageMemoryBarrier bar{};
                    bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    bar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    bar.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.image               = dstImg;
                    bar.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    bar.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                    bar.dstAccessMask       = 0;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
                });
        }

        if (!m_frameGraph.Compile()) {
            LOG_ERROR(Render, "Frame graph compile failed");
            return;
        }
        m_frameGraphBuilt = true;
    }

    vkResetCommandPool(m_vkDevice.Device(), fr.cmdPool, 0);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = 0;
    if (vkBeginCommandBuffer(fr.cmdBuffer, &cbbi) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkBeginCommandBuffer failed");
        return;
    }

    if (m_uploadBudget.IsValid())
        m_uploadBudget.ProcessFrame(fr.cmdBuffer, m_vkFrameResources.CurrentFrameIndex());

    if (m_vkShadowMap.IsValid()) {
        m_fgRegistry.SetImage(m_fgShadowMap0Id, m_vkShadowMap.GetImage(0));
        m_fgRegistry.SetView(m_fgShadowMap0Id, m_vkShadowMap.GetView(0));
        m_fgRegistry.SetImage(m_fgShadowMap1Id, m_vkShadowMap.GetImage(1));
        m_fgRegistry.SetView(m_fgShadowMap1Id, m_vkShadowMap.GetView(1));
        m_fgRegistry.SetImage(m_fgShadowMap2Id, m_vkShadowMap.GetImage(2));
        m_fgRegistry.SetView(m_fgShadowMap2Id, m_vkShadowMap.GetView(2));
        m_fgRegistry.SetImage(m_fgShadowMap3Id, m_vkShadowMap.GetImage(3));
        m_fgRegistry.SetView(m_fgShadowMap3Id, m_vkShadowMap.GetView(3));
    }
    m_fgRegistry.SetImage(m_fgGBufferAId, m_vkGBuffer.GetImageA());
    m_fgRegistry.SetImage(m_fgGBufferBId, m_vkGBuffer.GetImageB());
    m_fgRegistry.SetImage(m_fgGBufferCId, m_vkGBuffer.GetImageC());
    m_fgRegistry.SetImage(m_fgGBufferVelocityId, m_vkGBuffer.GetImageD());
    m_fgRegistry.SetImage(m_fgDepthId, m_vkGBuffer.GetImageDepth());
    if (m_vkSsaoRaw.IsValid()) {
        m_fgRegistry.SetImage(m_fgSsaoRawId, m_vkSsaoRaw.GetImage());
        m_fgRegistry.SetView(m_fgSsaoRawId, m_vkSsaoRaw.GetImageView());
    }
    if (m_vkSsaoBlur.IsValid()) {
        m_fgRegistry.SetImage(m_fgSsaoBlurTempId, m_vkSsaoBlur.GetImageTemp());
        m_fgRegistry.SetView(m_fgSsaoBlurTempId, m_vkSsaoBlur.GetImageViewTemp());
        m_fgRegistry.SetImage(m_fgSsaoBlurId, m_vkSsaoBlur.GetImageOutput());
        m_fgRegistry.SetView(m_fgSsaoBlurId, m_vkSsaoBlur.GetImageViewOutput());
    }
    m_fgRegistry.SetImage(m_fgSceneColorHDRId, m_vkSceneColorHDR.GetImage());
    if (m_vkBloomPyramid.IsValid()) {
        for (uint32_t i = 0; i < kBloomMipCount; ++i)
            m_fgRegistry.SetImage(m_fgBloomMipId[i], m_vkBloomPyramid.GetImage(i));
    }
    if (m_fgBloomCombineId != ::engine::render::kInvalidResourceId)
        m_fgRegistry.SetImage(m_fgBloomCombineId, m_vkBloomCombineTarget.GetImage());
    m_fgRegistry.SetImage(m_fgSceneColorId, m_vkSceneColor.GetImage());
    if (m_vkTaaHistory.IsValid()) {
        m_fgRegistry.SetImage(m_fgTaaHistoryAId, m_vkTaaHistory.GetImage(0));
        m_fgRegistry.SetImage(m_fgTaaHistoryBId, m_vkTaaHistory.GetImage(1));
    }
    if (m_fgTaaOutputId != ::engine::render::kInvalidResourceId && m_vkTaaOutput.IsValid())
        m_fgRegistry.SetImage(m_fgTaaOutputId, m_vkTaaOutput.GetImage());
    m_fgRegistry.SetImage(m_fgSwapchainId, m_vkSwapchain.GetImage(imageIndex));
    m_chunkStats.BeginFrame();
    m_hlodDrawsThisFrame = 0u;
    m_instanceDrawsThisFrame = 0u;
    m_frameGraph.Execute(fr.cmdBuffer, m_fgRegistry);
    if (::engine::core::Time::FrameIndex() % 60u == 0u)
        m_chunkStats.LogFrameStats();
    if (::engine::core::Config::GetBool("debug.hlod_overlay", false) && ::engine::core::Time::FrameIndex() % 60u == 0u)
        LOG_INFO(Render, "HLOD overlay: hlod_draws={} instance_draws={}", m_hlodDrawsThisFrame, m_instanceDrawsThisFrame);

    if (m_vkTaaHistory.IsValid())
        m_taaHistoryIdx ^= 1u;

    for (int i = 0; i < 16; ++i)
        m_prevModelMatrix[i] = m_currModelMatrix[i];

    if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkEndCommandBuffer failed");
        return;
    }

    vkResetFences(m_vkDevice.Device(), 1, &fr.inFlightFence);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &fr.imageAvailable;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &fr.cmdBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &fr.renderFinished;

    if (vkQueueSubmit(m_vkDevice.GraphicsQueue(), 1, &si, fr.inFlightFence) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkQueueSubmit failed");
        return;
    }

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &fr.renderFinished;
    pi.swapchainCount     = 1;
    pi.pSwapchains       = &m_vkSwapchain.Get();
    pi.pImageIndices     = &imageIndex;

    const VkResult presentRes = vkQueuePresentKHR(m_vkDevice.PresentQueue(), &pi);

    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
        m_vkSwapchain.RequestRecreate();
    } else if (presentRes != VK_SUCCESS) {
        LOG_ERROR(Render, "vkQueuePresentKHR failed (code {})", static_cast<int>(presentRes));
    }

    m_vkFrameResources.AdvanceFrame();
}

void Engine::EndFrame() {
    Time::EndFrame();

    const double now = Time::ElapsedSeconds();
    if (now - m_lastFpsLogTime >= 1.0) {
        m_lastFpsLogTime = now;
        LOG_INFO(Core, "Frame {} — FPS={:.1f} dt={:.3f} ms",
                 Time::FrameIndex(),
                 Time::FPS(),
                 Time::DeltaMilliseconds());
    }

    // Simple frame pacing when running without vsync and a target FPS is set.
    if (!m_headless && !m_vsyncEnabled && m_targetFrameTime > 0.0) {
        const double frameSeconds = static_cast<double>(Time::DeltaSeconds());
        const double remaining    = m_targetFrameTime - frameSeconds;
        if (remaining > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
        }
    }
}

// ---------------------------------------------------------------------------
// Engine — hooks
// ---------------------------------------------------------------------------

void Engine::OnResize(int width, int height) {
    m_framebufferWidth  = width;
    m_framebufferHeight = height;
    m_taaResetHistory = true;
    m_taaCopyHistoryOnReset = true;
    if (m_vkSwapchain.IsValid()) {
        m_vkSwapchain.RequestRecreate();
    }
    LOG_INFO(Platform, "OnResize: {}×{}", width, height);
}

void Engine::OnQuit() {
    m_running = false;
    LOG_INFO(Core, "Quit requested — shutting down main loop");
}

