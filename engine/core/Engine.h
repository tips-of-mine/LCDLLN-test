#pragma once

/**
 * @file Engine.h
 * @brief High-level engine orchestrator and main game loop.
 *
 * Ticket: M00.5 — Game Loop: update/render, double-buffer render state.
 *
 * Responsibilities:
 *   - Own the main run loop (Config → subsystems → loop → shutdown).
 *   - Separate Update (simulation) from Render (presentation).
 *   - Maintain a double-buffered RenderState[2] (producer/consumer pattern).
 *   - Reset per-frame arenas and collect input in BeginFrame().
 *   - Provide hooks for window resize and quit events.
 */

#include "engine/core/FrameArena.h"
#include "engine/math/Frustum.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/vk/VkFrameResources.h"
#include "engine/render/vk/VkSceneColor.h"
#include "engine/render/vk/VkGBuffer.h"
#include "engine/render/vk/VkGeometryPipeline.h"
#include "engine/render/vk/VkSceneColorHDR.h"
#include "engine/render/vk/VkLightingPipeline.h"
#include "engine/render/vk/VkTonemapPipeline.h"
#include "engine/render/vk/VkMaterial.h"
#include "engine/render/vk/VkTextureLoader.h"
#include "engine/render/ShaderCache.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace engine::core {

// ---------------------------------------------------------------------------
// Minimal math and render state types (M03.0: Camera from engine/render, Frustum from engine/math)
// ---------------------------------------------------------------------------

/**
 * @brief Minimal 4×4 matrix storage for view/projection.
 *
 * No arithmetic helpers are provided here; RenderState simply exposes
 * storage that a future render module can fill.
 */
struct Mat4 {
    float m[16]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

/**
 * @brief Per-frame render state produced by Update and consumed by Render.
 *
 * The Update step fully writes one RenderState instance (the "write" slot)
 * while the Render step reads from another (the "read" slot).  Slots are
 * swapped atomically once Update has finished writing.
 */
struct RenderState {
    /// Camera (position, yaw, pitch, FOV, aspect, near, far) — M03.0.
    ::engine::render::Camera camera{};
    /// View matrix (column-major).
    Mat4   view{};
    /// Projection matrix (column-major, Vulkan).
    Mat4   proj{};
    /// Frustum planes for culling (from view*proj).
    ::engine::math::Frustum frustum{};

    /// Reserved field for a future draw-list representation.
    std::uint64_t drawListPlaceholder = 0;
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

/**
 * @brief Top-level engine object driving the main loop.
 *
 * Engine coordinates core subsystems (time, memory arenas, input, window)
 * and runs a stable game loop with clear Update/Render separation.
 */
class Engine {
public:
    /**
     * @brief Entry point called from main().
     *
     * Initialises configuration and core subsystems, runs the main loop
     * until exit is requested, then performs an orderly shutdown.
     *
     * @param argc Argument count from main().
     * @param argv Argument vector from main().
     * @return     Process exit code (0 on success, non-zero on failure).
     */
    static int Run(int argc, const char* const* argv);

private:
    /// Constructs an Engine with default configuration.
    Engine();

    /**
     * @brief Internal implementation of the run sequence.
     *
     * Called by the static Run() after global subsystems are ready.
     *
     * @param argc Argument count from main().
     * @param argv Argument vector from main().
     * @return     Process exit code.
     */
    int RunInternal(int argc, const char* const* argv);

    /**
     * @brief Per-frame entry point.
     *
     * Responsibilities:
     *   - Advance the time system (Time::BeginFrame).
     *   - Reset the per-frame arena for the current frame.
     *   - Poll window events and snapshot input state.
     */
    void BeginFrame();

    /**
     * @brief Simulation/update step.
     *
     * This function prepares all data required for rendering the frame:
     * it writes to the "write" RenderState slot in a producer fashion,
     * then atomically exposes the new slot to the Render step.
     */
    void Update();

    /**
     * @brief Rendering step.
     *
     * Consumes the latest RenderState published by Update using the
     * "read" slot index.  Currently this function contains placeholder
     * behaviour only (no real rendering yet).
     */
    void Render();

    /**
     * @brief End-of-frame hook.
     *
     * Used to perform periodic logging (FPS once per second) and apply
     * simple frame pacing when running without vsync.
     */
    void EndFrame();

    /**
     * @brief Handles window resize notifications.
     *
     * Called from the platform layer when the framebuffer size changes.
     * The new dimensions are recorded so future RenderState instances
     * can reflect the updated aspect ratio.
     *
     * @param width  New framebuffer width in pixels.
     * @param height New framebuffer height in pixels.
     */
    void OnResize(int width, int height);

    /**
     * @brief Handles quit requests (e.g. window close button).
     *
     * Sets an internal flag that causes the main loop to exit cleanly
     * at the next opportunity.
     */
    void OnQuit();

    // Non-copyable, non-movable.
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&)                 = delete;
    Engine& operator=(Engine&&)      = delete;

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    /// Per-frame scratch memory (double-buffered).
    memory::FrameArena<2> m_frameArenas;

    /// Double-buffered render state array (2 slots).
    std::array<RenderState, 2> m_renderStates{};

    /// Index of the slot currently visible to the Render step.
    std::atomic<std::uint32_t> m_renderReadIndex{ 0 };

    /// Index of the slot that Update will write to on the next frame.
    std::uint32_t m_renderWriteIndex = 1;

    /// Main window used by the game loop.
    platform::Window m_window;

    /// Vulkan instance + surface (M01.1).
    ::engine::render::vk::Instance m_vkInstance;

    /// Vulkan physical/logical device and queues (M01.2).
    ::engine::render::vk::VkDeviceContext m_vkDevice;

    /// Vulkan swapchain, image views, render pass, framebuffers (M01.3).
    ::engine::render::vk::VkSwapchain m_vkSwapchain;

    /// Vulkan frame resources: cmd pools, cmd buffers, semaphores, fences (M01.4).
    ::engine::render::vk::VkFrameResources m_vkFrameResources;

    /// Camera and FPS controller (M03.0).
    ::engine::render::Camera          m_camera{};
    ::engine::render::CameraController m_cameraController;

    /// Offscreen SceneColor target; resize with swapchain (M02.4).
    ::engine::render::vk::VkSceneColor m_vkSceneColor;

    /// GBuffer (A/B/C + Depth) for geometry pass (M03.1).
    ::engine::render::vk::VkGBuffer m_vkGBuffer;

    /// Shader cache for loading geometry shaders (M03.1).
    ::engine::render::ShaderCache m_shaderCache;

    /// Geometry pass pipeline (M03.1).
    ::engine::render::vk::VkGeometryPipeline m_geometryPipeline;

    /// Cube test mesh: vertex buffer and memory (M03.1).
    VkBuffer m_cubeVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cubeVertexBufferMemory = VK_NULL_HANDLE;
    uint32_t m_cubeVertexCount = 0;

    /// Texture loader + default material (M03.3).
    ::engine::render::vk::VkTextureLoader m_textureLoader;
    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;
    VkSampler m_materialSampler = VK_NULL_HANDLE;
    ::engine::render::vk::VkMaterial m_defaultMaterial;

    /// SceneColor_HDR for lighting output; tonemap reads it (M03.2).
    ::engine::render::vk::VkSceneColorHDR m_vkSceneColorHDR;
    ::engine::render::vk::VkLightingPipeline m_lightingPipeline;
    ::engine::render::vk::VkTonemapPipeline m_tonemapPipeline;

    /// Frame graph: Geometry → Lighting → Tonemap → Present (M02.4, M03.1, M03.2).
    ::engine::render::FrameGraph m_frameGraph;
    ::engine::render::Registry  m_fgRegistry;
    ::engine::render::ResourceId m_fgSceneColorId = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSwapchainId  = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferAId   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferBId   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferCId    = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgDepthId       = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSceneColorHDRId = ::engine::render::kInvalidResourceId;
    bool m_frameGraphBuilt = false;

    /// True when the window was successfully created.
    bool m_windowOk = false;

    /// Overall run flag; cleared by OnQuit().
    bool m_running = false;

    /// Headless mode flag (no window / input). Read from config.
    bool m_headless = false;

    /// Whether a fixed-timestep update loop is enabled.
    bool  m_useFixedTimestep = false;
    /// Fixed update step duration in seconds (when enabled).
    float m_fixedDeltaSeconds = 1.0f / 60.0f;
    /// Accumulator used by the fixed-timestep scheme.
    float m_fixedAccumulator  = 0.0f;

    /// Target frame time in seconds for simple frame pacing (0 = uncapped).
    double m_targetFrameTime = 0.0;

    /// Whether vsync is enabled according to configuration.
    bool m_vsyncEnabled = true;

    /// Last timestamp at which FPS was logged (seconds since Time::Init()).
    double m_lastFpsLogTime = 0.0;

    /// Latest known framebuffer width (for aspect ratio computation).
    int m_framebufferWidth  = 0;

    /// Latest known framebuffer height (for aspect ratio computation).
    int m_framebufferHeight = 0;
};

} // namespace engine::core

/**
 * @brief Convenience alias so callers can refer to engine::Engine.
 */
namespace engine {
using Engine = core::Engine;
} // namespace engine

