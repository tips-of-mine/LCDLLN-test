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
#include "engine/render/vk/VkShadowMap.h"
#include "engine/render/vk/VkShadowPipeline.h"
#include "engine/render/vk/VkBrdfLut.h"
#include "engine/render/vk/VkIrradianceCubemap.h"
#include "engine/render/vk/VkPrefilteredEnvCubemap.h"
#include "engine/render/vk/VkSsaoKernelNoise.h"
#include "engine/render/vk/VkSsaoRaw.h"
#include "engine/render/vk/VkSsaoPipeline.h"
#include "engine/render/vk/VkSsaoBlur.h"
#include "engine/render/vk/VkSsaoBlurPipeline.h"
#include "engine/render/vk/VkTaaHistory.h"
#include "engine/render/vk/VkTaaOutput.h"
#include "engine/render/vk/VkTaaPipeline.h"
#include "engine/render/vk/VkBloomPyramid.h"
#include "engine/render/vk/VkBloomPrefilterPipeline.h"
#include "engine/render/vk/VkBloomDownsamplePipeline.h"
#include "engine/render/vk/VkBloomUpsamplePipeline.h"
#include "engine/render/vk/VkBloomCombineTarget.h"
#include "engine/render/vk/VkBloomCombinePipeline.h"
#include "engine/render/vk/VkExposureReduce.h"
#include "engine/render/vk/VkParticlePass.h"
#include "engine/render/vk/VkParticlePipeline.h"
#include "engine/render/ParticleSystem.h"
#include "engine/render/Csm.h"
#include "engine/render/ShaderCache.h"
#include "engine/world/ChunkStats.h"
#include "engine/world/HlodRuntime.h"
#include "engine/world/ZoneBuildFormat.h"
#include "engine/world/NavMeshFormat.h"
#include "engine/world/ProbesFormat.h"
#include "engine/streaming/StreamingScheduler.h"
#include "engine/streaming/LruCache.h"
#include "engine/render/vk/DeferredDestroyQueue.h"
#include "engine/render/vk/VkUploadBudget.h"
#include "engine/editor/EditorUI.h"
#include "engine/ui/GameHud.h"
#include "engine/world/GameplayVolume.h"

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

    /// M07.1 — TAA: curr/prev ViewProj (column-major) and jitter in NDC for reprojection.
    float viewProjCurr[16]{};
    float viewProjPrev[16]{};
    float jitterCurr[2]{0.0f, 0.0f};
    float jitterPrev[2]{0.0f, 0.0f};

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

    /**
     * @brief Zone transition hook (M13.4): reset streaming, preload new zone, set camera to spawn pos, reset TAA history.
     *
     * Call when server sends ZoneChange. Client does not decide zone; server validates.
     *
     * @param zoneId   Target zone id (used to resolve zone path, e.g. zones/zone_001).
     * @param spawnPos Spawn position [x,y,z] in the new zone (camera is moved here).
     */
    void OnZoneChange(std::int32_t zoneId, const float spawnPos[3]);

    /**
     * @brief Sets HUD data for the next frame (player/target HP, combat log). M16.2.
     * Call each frame from game layer when not in editor mode to drive HUD updates.
     */
    void SetHudData(const ::engine::ui::HudData& data);

    /**
     * @brief Switches UI theme (e.g. by race). Loads contentRoot/ui/themes/themeName/theme.json + style.qss. M16.5.
     * @param themeName Theme folder name (e.g. "default", "human", "elf"). Path resolved via paths.content.
     */
    void SetTheme(const std::string& themeName);

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
    /// M17.2 — Particles: pass (HDR+depth), pipeline (billboard blend), pool, quad VB, instance buffer.
    ::engine::render::vk::VkParticlePass m_particlePass;
    ::engine::render::vk::VkParticlePipeline m_particlePipeline;
    ::engine::render::ParticlePool m_particlePool;
    ::engine::render::ParticleEmitterDef m_particleEmitterDef{};
    float m_particleSpawnPosition[3] = {0.f, 1.f, 0.f};
    VkBuffer m_particleQuadVB = VK_NULL_HANDLE;
    VkDeviceMemory m_particleQuadVBMemory = VK_NULL_HANDLE;
    VkBuffer m_particleInstanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleInstanceMemory = VK_NULL_HANDLE;

    /// Shadow maps: 4 cascades, depth-only (M04.2).
    ::engine::render::vk::VkShadowMap m_vkShadowMap;
    ::engine::render::vk::VkShadowPipeline m_shadowPipeline;
    ::engine::render::CsmUniform m_csmUniform{};

    /// BRDF LUT 256x256 RG16F, compute-generated at boot (M05.1).
    ::engine::render::vk::VkBrdfLut m_vkBrdfLut;
    /// Irradiance cubemap (64x64 RGBA16F) from env HDR; fallback 1x1 cube if env not loaded (M05.2).
    ::engine::render::vk::VkIrradianceCubemap m_vkIrradianceCubemap;
    /// Prefiltered specular cubemap (256 RGBA16F + mips) from env HDR (M05.3).
    ::engine::render::vk::VkPrefilteredEnvCubemap m_vkPrefilteredEnvCubemap;
    /// Sampler for env/irradiance cubemap (convolution source and default cube fallback).
    VkSampler m_envCubemapSampler = VK_NULL_HANDLE;
    /// SSAO kernel UBO + 4x4 noise texture, generated at boot (M06.1).
    ::engine::render::vk::VkSsaoKernelNoise m_ssaoKernelNoise;
    /// SSAO_Raw render target (R16F) and generate pass pipeline (M06.2).
    ::engine::render::vk::VkSsaoRaw m_vkSsaoRaw;
    ::engine::render::vk::VkSsaoPipeline m_ssaoPipeline;
    /// SSAO_Blur temp + output (R16F) and bilateral blur pipeline (M06.3).
    ::engine::render::vk::VkSsaoBlur m_vkSsaoBlur;
    ::engine::render::vk::VkSsaoBlurPipeline m_ssaoBlurPipeline;
    uint32_t m_shadowMapSize = 1024u;

    /// Frame graph: Shadow0..3 → Geometry → Lighting → Tonemap → Present (M02.4, M03.1, M03.2, M04.2).
    ::engine::render::FrameGraph m_frameGraph;
    ::engine::render::Registry  m_fgRegistry;
    ::engine::render::ResourceId m_fgSceneColorId = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSwapchainId  = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferAId   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferBId   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferCId      = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgGBufferVelocityId = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgDepthId         = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSceneColorHDRId = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgShadowMap0Id   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgShadowMap1Id   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgShadowMap2Id   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgShadowMap3Id   = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSsaoRawId       = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSsaoBlurTempId  = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgSsaoBlurId     = ::engine::render::kInvalidResourceId;
    /// TAA history ping-pong (M07.2): HistoryA = 0, HistoryB = 1.
    ::engine::render::vk::VkTaaHistory m_vkTaaHistory;
    ::engine::render::ResourceId m_fgTaaHistoryAId  = ::engine::render::kInvalidResourceId;
    ::engine::render::ResourceId m_fgTaaHistoryBId  = ::engine::render::kInvalidResourceId;
    /// TAA pass output (M07.4); then copied to HistoryNext, used for Present.
    ::engine::render::vk::VkTaaOutput m_vkTaaOutput;
    ::engine::render::vk::VkTaaPipeline m_taaPipeline;
    ::engine::render::ResourceId m_fgTaaOutputId = ::engine::render::kInvalidResourceId;
    /// Bloom pyramid 1/2..1/32 (M08.1).
    ::engine::render::vk::VkBloomPyramid m_vkBloomPyramid;
    ::engine::render::vk::VkBloomPrefilterPipeline m_bloomPrefilterPipeline;
    ::engine::render::vk::VkBloomDownsamplePipeline m_bloomDownsamplePipeline;
    ::engine::render::vk::VkBloomUpsamplePipeline m_bloomUpsamplePipeline;
    ::engine::render::vk::VkBloomCombineTarget m_vkBloomCombineTarget;
    ::engine::render::vk::VkBloomCombinePipeline m_bloomCombinePipeline;
    ::engine::render::ResourceId m_fgBloomMipId[::engine::render::vk::kBloomMipCount]{};
    ::engine::render::ResourceId m_fgBloomCombineId = ::engine::render::kInvalidResourceId;
    /// Auto-exposure: luminance reduce + temporal adapt (M08.3).
    ::engine::render::vk::VkExposureReduce m_vkExposureReduce;
    VkBuffer m_exposureFallbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_exposureFallbackMemory = VK_NULL_HANDLE;
    bool m_frameGraphBuilt = false;
    /// Per-chunk/per-ring drawcall and triangle stats (M09.2).
    ::engine::world::ChunkStats m_chunkStats;
    /// HLOD vs instance draw counts this frame for debug overlay (M09.5).
    uint32_t m_hlodDrawsThisFrame = 0u;
    uint32_t m_instanceDrawsThisFrame = 0u;
    /// Streaming scheduler: request/io/cpu/gpuUpload queues + priority (M10.1).
    ::engine::streaming::StreamingScheduler m_streamingScheduler;
    /// LRU cache for decompressed blobs (IO stage: hit -> skip disk) (M10.3).
    ::engine::streaming::LruCache m_lruCache;
    /// Deferred GPU resource destruction (collect at BeginFrame when fence signaled) (M10.3).
    ::engine::render::vk::DeferredDestroyQueue m_deferredDestroyQueue;
    /// GPU upload budget per frame + staging ring (M10.4).
    ::engine::render::vk::VkUploadBudget m_uploadBudget;
    /// M11.2: loaded zone chunk instances for placeholder display (from zone.build_path).
    std::vector<::engine::world::ZoneChunkInstance> m_zoneChunkInstances;
    ::engine::world::NavMeshData m_zoneNavMesh;
    std::vector<::engine::world::NavMeshPortal> m_zoneNavPortals;
    std::vector<::engine::world::ZoneProbe> m_zoneProbes;
    ::engine::world::ZoneAtmosphere m_zoneAtmosphere;
    bool m_zoneBuildLoaded = false;
    /// M13.4 — When non-empty, used instead of zone.build_path for zone load (e.g. "zones/zone_002" after ZoneChange).
    std::string m_zonePathOverride;

    /// True when the window was successfully created.
    bool m_windowOk = false;

    /// Overall run flag; cleared by OnQuit().
    bool m_running = false;

    /// Headless mode flag (no window / input). Read from config.
    bool m_headless = false;

    /// M12.1 — Editor mode (--editor or config editor: true). ImGui + selection + gizmos.
    bool m_editor = false;
    /// Selected zone instance index (-1 = none). Used when m_editor is true.
    int m_editorSelectedInstanceIndex = -1;
    /// Editor UI (ImGui). Initialized when m_editor and swapchain valid.
    ::engine::editor::EditorUI m_editorUI;
    /// M12.2 — Path from which zone chunk instances were loaded (for Save).
    std::string m_zoneChunkInstancesPath;
    /// M12.2 — True when editor has unsaved changes (position/layer/etc).
    bool m_editorDirty = false;
    /// M12.2 — Save requested by editor UI (cleared after handling).
    bool m_editorSaveRequested = false;
    /// M12.2 — Layer visibility (instance layer = flags & 0x0F). Default all true.
    bool m_editorLayerVisible[16] = {};
    /// M12.2 — Layer locked (no edit when locked). Default all false.
    bool m_editorLayerLocked[16] = {};
    /// M12.3 — Zone base path (content + zone.build_path) for volumes.json load/save.
    std::string m_zoneBasePath;
    /// M12.3 — Gameplay volumes (triggers, spawns, transitions); loaded from volumes.json.
    std::vector<::engine::world::GameplayVolume> m_zoneVolumes;
    /// M12.3 — Selected volume index (-1 = none).
    int m_editorSelectedVolumeIndex = -1;
    /// M12.3 — Export volumes requested by editor UI (write volumes.json).
    bool m_editorExportVolumesRequested = false;
    /// M12.4 — Export layout requested by editor UI (write layout.json).
    bool m_editorExportLayoutRequested = false;

    /// M16.2 — Game HUD (when not in editor): player/target bars, combat log.
    ::engine::ui::GameHud m_gameHud;
    /// M16.2 — HUD data fed each frame (player HP, target, combat log); default 100/100, no target.
    ::engine::ui::HudData m_hudData;
    /// M16.5 — Theme manager: theme.json + style.qss, apply to ImGui.
    ::engine::ui::ThemeManager m_themeManager;
    /// M16.5 — Current theme name (e.g. "default") for LoadTheme / hot-reload.
    std::string m_themeName = "default";

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

    /// M07.1 — TAA: Halton sample index (mod kHaltonSequenceSize); reset history on resize/FOV.
    uint32_t m_taaFrameIndex = 0u;
    bool m_taaResetHistory = true;
    float m_taaPrevAspect = 0.0f;
    float m_taaPrevFov = 0.0f;
    /// M07.2 — TAA: ping-pong index (next=idx, prev=idx^1); swap each frame after render.
    uint32_t m_taaHistoryIdx = 0u;
    /// First frame: copy current to both history buffers.
    bool m_taaFirstFrame = true;
    /// On reset: copy current -> history next (set in OnResize / Update when reset detected).
    bool m_taaCopyHistoryOnReset = false;
    /// M07.3 — Motion vectors: prev/curr model matrix per rigid (column-major); updated at end of Render.
    float m_prevModelMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float m_currModelMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

} // namespace engine::core

/**
 * @brief Convenience alias so callers can refer to engine::Engine.
 */
namespace engine {
using Engine = core::Engine;
} // namespace engine

