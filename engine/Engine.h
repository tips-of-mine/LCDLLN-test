#pragma once

#include "engine/core/Config.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/audio/AudioEngine.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/AssetRegistry.h"
#include "engine/render/DecalSystem.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkFrameSync.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/ShaderCache.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/render/ShaderHotReload.h"
#include "engine/render/TaaJitter.h"
#include "engine/render/Camera.h"
#include "engine/render/CascadedShadowMaps.h"
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"
#include "engine/world/WorldModel.h"
#include "engine/world/ChunkBudgetStats.h"
#include "engine/world/LodConfig.h"
#include "engine/world/HlodRuntime.h"
#include "engine/world/ProbeData.h"
#include "engine/world/StreamingScheduler.h"
#include "engine/world/StreamCache.h"
#include "engine/render/vk/DeferredDestroyQueue.h"
#include "engine/render/GpuUploadQueue.h"
#include "engine/render/vk/StagingAllocator.h"

struct GLFWwindow;

namespace engine::render { class DeferredPipeline; }
namespace engine::editor { class EditorMode; }

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine
{
	/// Minimal render state produced by Update and consumed by Render.
	struct RenderState
	{
		engine::render::Camera camera;
		engine::math::Mat4 viewMatrix;
		engine::math::Mat4 projMatrix;
		engine::math::Mat4 viewProjMatrix;
		/// M07.1: ViewProj from previous frame (for TAA reprojection).
		engine::math::Mat4 prevViewProjMatrix;
		/// M07.1: Current frame jitter in NDC (x, y), applied to projection.
		float jitterCurrNdc[2]{ 0.0f, 0.0f };
		engine::math::Frustum frustum;
		engine::render::CascadesUniform cascades;
		float objectModelMatrix[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		bool objectVisible = true;

		// Placeholder draw-list marker.
		uint32_t drawItemCount = 0;
		/// M09.5: Debug overlay HLOD state (e.g. "HLOD: 3 inst: 5 culled: 2").
		std::string hlodDebugText;
	};

	/// Engine loop: BeginFrame/Update/Render/EndFrame with double-buffered RenderState.
	class Engine final
	{
	public:
		Engine(int argc, char** argv);
		~Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		/// Run the main loop until quit is requested.
		int Run();

		/// Registers a shader for hot-reload (path relative to paths.content, e.g. "shaders/foo.vert").
		void WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines = {});

		/// Returns the shader cache (e.g. to get SPIR-V by key).
		engine::render::ShaderCache& GetShaderCache() { return m_shaderCache; }
		const engine::render::ShaderCache& GetShaderCache() const { return m_shaderCache; }

		/// M07.2: TAA history ping-pong. next = write target this frame, prev = read (previous frame).
		engine::render::ResourceId GetTaaHistoryPrevId() const;
		engine::render::ResourceId GetTaaHistoryNextId() const;

	private:
		void BeginFrame();
		void Update();
		void Render();
		void EndFrame();

		void SwapRenderState();

		void OnResize(int w, int h);
		void OnQuit();
		/// Load optional zone probe and atmosphere assets from content-relative paths.
		void LoadZoneProbeAssets();

		engine::core::Config m_cfg;

		engine::platform::Window m_window;
		engine::platform::Input m_input;

		engine::render::VkInstance m_vkInstance;
		engine::render::VkDeviceContext m_vkDeviceContext;
		engine::render::VkSwapchain m_vkSwapchain;
		std::array<engine::render::FrameResources, 2> m_frameResources{};
		engine::render::ShaderCache m_shaderCache;
		engine::render::ShaderHotReload m_shaderHotReload;
		uint32_t m_currentFrame = 0;
		GLFWwindow* m_glfwWindowForVk = nullptr;
		bool m_swapchainResizeRequested = false;

		engine::render::FrameGraph m_frameGraph;
		engine::render::Registry m_fgRegistry;
		engine::render::ResourceId m_fgSceneColorId      = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgBackbufferId      = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferAId        = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgDecalOverlayId    = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferBId        = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferCId        = engine::render::kInvalidResourceId;
		/// M07.3: velocity buffer (currNDC - prevNDC), R16G16F.
		engine::render::ResourceId m_fgGBufferVelocityId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgDepthId           = engine::render::kInvalidResourceId;
		/// SceneColor_HDR: output of the deferred lighting pass (R16G16B16A16_SFLOAT). Added in M03.2.
		engine::render::ResourceId m_fgSceneColorHDRId   = engine::render::kInvalidResourceId;
		/// SceneColor_LDR: output of the tonemap pass (R8G8B8A8_UNORM). Added in M03.4.
		engine::render::ResourceId m_fgSceneColorLDRId   = engine::render::kInvalidResourceId;
		/// M08.2: SceneColor_HDR + bloom (combine pass output); tonemap reads this.
		engine::render::ResourceId m_fgSceneColorHDRWithBloomId = engine::render::kInvalidResourceId;
		/// SSAO_Raw: output of SSAO generate pass (R16F). M06.2.
		engine::render::ResourceId m_fgSsaoRawId        = engine::render::kInvalidResourceId;
		/// SSAO_Blur_Temp: intermediate for bilateral blur H pass. M06.3.
		engine::render::ResourceId m_fgSsaoBlurTempId   = engine::render::kInvalidResourceId;
		/// SSAO_Blur: output of bilateral blur (2 passes). M06.3.
		engine::render::ResourceId m_fgSsaoBlurId       = engine::render::kInvalidResourceId;
		/// M07.2: TAA history ping-pong (format LDR = input TAA format).
		engine::render::ResourceId m_fgHistoryAId       = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgHistoryBId        = engine::render::kInvalidResourceId;
		/// Shadow maps per cascade (depth + sampled). Added in M04.2.
		std::array<engine::render::ResourceId, engine::render::kCascadeCount> m_fgShadowMapIds{};
		/// M08.1: Bloom mip pyramid (BloomMip0 = full res, .. BloomMip5 = 1/32).
		std::array<engine::render::ResourceId, engine::render::kBloomMipCount> m_fgBloomDownMipIds{};
		std::array<engine::render::ResourceId, engine::render::kBloomMipCount> m_fgBloomUpMipIds{};

		/// All deferred passes (geometry, shadow, SSAO, lighting, bloom, tonemap, TAA). Init/Destroy in Engine.cpp.
		std::unique_ptr<engine::render::DeferredPipeline> m_pipeline;
		engine::render::MeshHandle m_geometryMeshHandle;
		engine::render::DecalSystem m_decalSystem;
		std::vector<engine::render::VisibleDecal> m_visibleDecals;

		engine::render::AssetRegistry m_assetRegistry;
		std::unique_ptr<engine::editor::EditorMode> m_editorMode;
		/// M08.4: Optional color grading LUT (strip 256x16 .texr). Loaded from config color_grading.lut_path.
		engine::render::TextureHandle m_colorGradingLutHandle;
		/// Centralised GPU allocator (VMA). Opaque pointer; cast to VmaAllocator in Engine.cpp.
		void* m_vmaAllocator = nullptr;
		engine::audio::AudioEngine m_audioEngine;

		engine::core::Time m_time;
		engine::core::memory::FrameArena m_frameArena;
		engine::render::FpsCameraController m_fpsCameraController;
		engine::world::World m_world;
		engine::world::StreamingScheduler m_streamingScheduler;
		engine::world::StreamCache m_streamCache;
		engine::render::DeferredDestroyQueue m_deferredDestroyQueue;
		engine::render::GpuUploadQueue m_gpuUploadQueue;
		engine::render::StagingAllocator m_stagingAllocator;
		engine::world::ChunkBudgetStats m_chunkStats;
		engine::world::LodConfig m_lodConfig;
		engine::world::HlodRuntime m_hlodRuntime;
		engine::world::ProbeSet m_zoneProbes;
		engine::world::AtmosphereSettings m_zoneAtmosphere;
		std::vector<engine::world::ChunkDrawDecision> m_chunkDrawDecisions;

		std::array<RenderState, 2> m_renderStates{};
		std::atomic<uint32_t> m_renderReadIndex{ 0 };

		bool m_quitRequested = false;
		bool m_editorEnabled = false;
		bool m_vsync = true;
		double m_fixedDt = 0.0;
		int m_width = 0;
		int m_height = 0;
		/// M07.1: When true, TAA prev history is invalid (resize/FOV/teleport); next frame prev = curr.
		bool m_taaHistoryInvalid = true;
		/// M07.2: True after first TAA history init (both buffers filled); on reset we copy only to next.
		bool m_taaHistoryEverFilled = false;
	};
}
