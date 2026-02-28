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
#include "engine/platform/Window.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/vk/VkFrameResources.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace engine::core {

// ---------------------------------------------------------------------------
// Minimal math and render state types
// ---------------------------------------------------------------------------

/**
 * @brief Minimal camera description stored in the RenderState.
 *
 * This is intentionally lightweight: it carries just enough information
 * for basic view/projection setup.  More advanced camera behaviour is
 * implemented in later tickets.
 */
struct Camera {
    /// World-space position of the camera.
    float position[3]{ 0.0f, 0.0f, 0.0f };
    /// Forward (look) direction, normalised.
    float forward[3]{ 0.0f, 0.0f, -1.0f };
    /// Up vector, normalised.
    float up[3]{ 0.0f, 1.0f, 0.0f };

    /// Vertical field of view in radians.
    float fovY      = 60.0f * 3.1415926535f / 180.0f;
    /// Aspect ratio (width / height).
    float aspect    = 16.0f / 9.0f;
    /// Near plane distance.
    float nearPlane = 0.1f;
    /// Far plane distance.
    float farPlane  = 1000.0f;
};

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
    /// Camera parameters for the current frame.
    Camera camera{};
    /// View matrix corresponding to the camera.
    Mat4   view{};
    /// Projection matrix corresponding to the camera and window size.
    Mat4   proj{};

    /// Reserved field for a future draw-list representation.
    /// Kept empty on purpose for this ticket.
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

