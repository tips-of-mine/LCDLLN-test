#pragma once

#include "engine/core/Config.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/AssetRegistry.h"
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
#include "engine/render/ShadowMapPass.h"
#include "engine/render/BrdfLutPass.h"
#include "engine/render/SpecularPrefilterPass.h"
#include "engine/render/SsaoKernelNoise.h"
#include "engine/render/SsaoPass.h"
#include "engine/render/SsaoBlurPass.h"
#include "engine/render/GeometryPass.h"
#include "engine/render/LightingPass.h"
#include "engine/render/TonemapPass.h"      // M03.4: filmic tonemap HDR→LDR
#include "engine/render/BloomPass.h"        // M08.1: bloom prefilter + downsample pyramid
#include "engine/render/AutoExposure.h"      // M08.3: auto-exposure reduce luminance + temporal adapt
#include "engine/render/TaaPass.h"          // M07.4: TAA reprojection + clamp
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"

struct GLFWwindow;

#include <array>
#include <atomic>
#include <cstdint>

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

		// Placeholder draw-list marker.
		uint32_t drawItemCount = 0;
	};

	/// Engine loop: BeginFrame/Update/Render/EndFrame with double-buffered RenderState.
	class Engine final
	{
	public:
		Engine(int argc, char** argv);
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
		std::array<engine::render::ResourceId, engine::render::kBloomMipCount> m_fgBloomMipIds{};

		engine::render::GeometryPass m_geometryPass;
		engine::render::MeshHandle   m_geometryMeshHandle;

		/// Depth-only shadow map pass for cascades. Added in M04.2.
		engine::render::ShadowMapPass m_shadowMapPass;

		/// BRDF LUT compute pass (M05.1): generates 256x256 RG16F split-sum GGX LUT at startup.
		engine::render::BrdfLutPass m_brdfLutPass;
		/// Specular prefilter pass (M05.3): prefiltered GGX cubemap with mips = roughness.
		engine::render::SpecularPrefilterPass m_specularPrefilterPass;

		/// SSAO kernel + 4x4 noise (M06.1): UBO and tiled noise texture, generated at boot.
		engine::render::SsaoKernelNoise m_ssaoKernelNoise;
		/// SSAO generate pass (M06.2): depth + normal -> SSAO_Raw (R16F).
		engine::render::SsaoPass m_ssaoPass;
		/// SSAO bilateral blur (M06.3): 2 passes H then V, depth-aware.
		engine::render::SsaoBlurPass m_ssaoBlurPass;

		/// Deferred fullscreen lighting pass (PBR GGX). Added in M03.2.
		engine::render::LightingPass m_lightingPass;
		/// Filmic tonemap pass: SceneColor_HDR → SceneColor_LDR. Added in M03.4.
		engine::render::TonemapPass  m_tonemapPass;
		/// Bloom prefilter + downsample (M08.1): extract HDR highlights, pyramid 1/2..1/32.
		engine::render::BloomPrefilterPass  m_bloomPrefilterPass;
		engine::render::BloomDownsamplePass m_bloomDownsamplePass;
		engine::render::BloomUpsamplePass   m_bloomUpsamplePass;
		engine::render::BloomCombinePass    m_bloomCombinePass;
		engine::render::AutoExposure        m_autoExposure;
		/// TAA pass (M07.4): reproject history, clamp 3x3, blend.
		engine::render::TaaPass      m_taaPass;

		engine::render::AssetRegistry m_assetRegistry;
		/// M08.4: Optional color grading LUT (strip 256x16 .texr). Loaded from config color_grading.lut_path.
		engine::render::TextureHandle m_colorGradingLutHandle;

		engine::core::Time m_time;
		engine::core::memory::FrameArena m_frameArena;
		engine::render::FpsCameraController m_fpsCameraController;

		std::array<RenderState, 2> m_renderStates{};
		std::atomic<uint32_t> m_renderReadIndex{ 0 };

		bool m_quitRequested = false;
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
