#include "engine/core/Engine.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Memory.h"
#include "engine/core/MemoryTags.h"
#include "engine/core/Time.h"

#include "engine/platform/FileSystem.h"
#include "engine/platform/Input.h"
#include "engine/render/vk/VkFrameResources.h"
#include "engine/render/vk/VkSwapchain.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace engine::core;
using namespace engine::platform;
using namespace engine::core::memory;

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
        m_vkFrameResources.Shutdown();
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
    // Choose timestep: either variable or simple fixed step.
    const float dt = m_useFixedTimestep ? m_fixedDeltaSeconds
                                        : Time::DeltaSeconds();

    // Select the write slot and populate the next RenderState.
    const std::uint32_t writeIdx = m_renderWriteIndex;
    RenderState& rs = m_renderStates[writeIdx];

    // Basic camera setup: keep it static for now but ensure aspect is correct.
    const float w = (m_framebufferWidth  > 0) ? static_cast<float>(m_framebufferWidth)  : 1280.0f;
    const float h = (m_framebufferHeight > 0) ? static_cast<float>(m_framebufferHeight) : 720.0f;
    const float aspect = (h > 0.0f) ? (w / h) : (16.0f / 9.0f);

    rs.camera.aspect = aspect;

    (void)dt; // dt is currently unused; it will drive simulation in later tickets.

    // Atomically publish the write slot to the reader and flip for next frame.
    m_renderReadIndex.store(writeIdx, std::memory_order_release);
    m_renderWriteIndex = writeIdx ^ 1u;
}

void Engine::Render() {
    // Recreate swapchain on resize if requested.
    if (m_vkSwapchain.IsValid() && m_vkSwapchain.NeedsRecreate()) {
        const uint32_t w = (m_framebufferWidth  > 0) ? static_cast<uint32_t>(m_framebufferWidth)  : 1u;
        const uint32_t h = (m_framebufferHeight > 0) ? static_cast<uint32_t>(m_framebufferHeight) : 1u;
        m_vkSwapchain.Recreate(w, h);
    }

    // Skip Vulkan render if swapchain or frame resources invalid.
    if (!m_vkSwapchain.IsValid() || !m_vkFrameResources.IsValid()) {
        return;
    }

    using namespace engine::render::vk;
    FrameResources& fr = m_vkFrameResources.Current();

    // 1. Wait fence (ensures frame resources are not in use).
    if (vkWaitForFences(m_vkDevice.Device(), 1, &fr.inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkWaitForFences failed");
        return;
    }

    // 2. Acquire next swapchain image.
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

    // 3. Reset command pool and record.
    vkResetCommandPool(m_vkDevice.Device(), fr.cmdPool, 0);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = 0;
    if (vkBeginCommandBuffer(fr.cmdBuffer, &cbbi) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkBeginCommandBuffer failed");
        return;
    }

    VkClearValue clearColor{};
    clearColor.color.float32[0] = 0.1f;
    clearColor.color.float32[1] = 0.1f;
    clearColor.color.float32[2] = 0.15f;
    clearColor.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = m_vkSwapchain.RenderPass();
    rpbi.framebuffer       = m_vkSwapchain.Framebuffer(imageIndex);
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = m_vkSwapchain.Extent();
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clearColor;

    vkCmdBeginRenderPass(fr.cmdBuffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(fr.cmdBuffer);

    if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkEndCommandBuffer failed");
        return;
    }

    // 4. Reset fence before submit.
    vkResetFences(m_vkDevice.Device(), 1, &fr.inFlightFence);

    // 5. Submit.
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

    // 6. Present.
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount  = 1;
    pi.pWaitSemaphores     = &fr.renderFinished;
    pi.swapchainCount      = 1;
    pi.pSwapchains         = &m_vkSwapchain.Get();
    pi.pImageIndices       = &imageIndex;

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
    if (m_vkSwapchain.IsValid()) {
        m_vkSwapchain.RequestRecreate();
    }
    LOG_INFO(Platform, "OnResize: {}×{}", width, height);
}

void Engine::OnQuit() {
    m_running = false;
    LOG_INFO(Core, "Quit requested — shutting down main loop");
}

