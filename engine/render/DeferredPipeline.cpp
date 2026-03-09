#include "engine/render/DeferredPipeline.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
#include <cstdio>

namespace engine::render
{
	bool DeferredPipeline::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		const engine::core::Config& config,
		uint32_t shadowMapResolution,
		VkFormat sceneColorLDRFormat,
		VkQueue graphicsQueue,
		uint32_t graphicsQueueFamilyIndex,
		ShaderLoaderFn loadSpirv)
	{
		std::fprintf(stderr, "[PIPELINE] Init enter\n"); std::fflush(stderr);

		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || !loadSpirv)
			return false;

		// M05.1: BRDF LUT
		std::fprintf(stderr, "[PIPELINE] 1 BRDF LUT\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> brdfComp = loadSpirv("shaders/brdf_lut.comp.spv");
			if (!brdfComp.empty())
			{
				const uint32_t lutSize = 256u;
				if (m_brdfLutPass.Init(device, physicalDevice, vmaAllocator, lutSize,
						brdfComp.data(), brdfComp.size(), graphicsQueueFamilyIndex))
				{
					m_brdfLutPass.Generate(device, graphicsQueue);
					LOG_INFO(Render, "[Boot] DeferredPipeline BRDF LUT OK");
				}
				else
					LOG_WARN(Render, "M05.1: BRDF LUT init failed — LUT disabled");
			}
			else
				LOG_WARN(Render, "M05.1: BRDF LUT shader not found — LUT disabled");
		}

		// M05.3: Specular prefilter
		std::fprintf(stderr, "[PIPELINE] 2 SpecularPrefilter\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> specPrefilterComp = loadSpirv("shaders/specular_prefilter.comp.spv");
			if (!specPrefilterComp.empty())
			{
				const uint32_t specSize = 256u, specMipCount = 6u;
				if (m_specularPrefilterPass.Init(device, physicalDevice, vmaAllocator,
						specSize, specMipCount, specPrefilterComp.data(), specPrefilterComp.size(), graphicsQueueFamilyIndex))
					LOG_INFO(Render, "[Boot] DeferredPipeline SpecularPrefilter OK");
				else
					LOG_WARN(Render, "M05.3: Specular prefilter init failed — disabled");
			}
			else
				LOG_WARN(Render, "M05.3: specular_prefilter.comp not found — disabled");
		}

		// M06.1: SSAO kernel + noise
		std::fprintf(stderr, "[PIPELINE] 3 SSAO kernel+noise\n"); std::fflush(stderr);
		// m_ssaoKernelNoise.Init(device, physicalDevice, vmaAllocator, config, graphicsQueue, graphicsQueueFamilyIndex);
		std::fprintf(stderr, "[PIPELINE] 3 SSAO SKIPPED\n"); std::fflush(stderr);

		if (m_ssaoKernelNoise.IsValid())
			LOG_INFO(Render, "[Boot] DeferredPipeline SSAO kernel/noise OK");

		// M06.2: SSAO generate
		std::fprintf(stderr, "[PIPELINE] 4 SSAO pass\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> ssaoVert = loadSpirv("shaders/ssao.vert.spv");
			std::vector<uint32_t> ssaoFrag = loadSpirv("shaders/ssao.frag.spv");
			if (!ssaoVert.empty() && !ssaoFrag.empty() && m_ssaoKernelNoise.IsValid())
			{
				if (!m_ssaoPass.Init(device, physicalDevice, VK_FORMAT_R16_SFLOAT,
						ssaoVert.data(), ssaoVert.size(), ssaoFrag.data(), ssaoFrag.size(), 2))
					LOG_WARN(Render, "M06.2: SSAO pass init failed");
				else
					LOG_INFO(Render, "M06.2: SSAO generate pass ready");
			}
			else if (!m_ssaoKernelNoise.IsValid())
				LOG_WARN(Render, "M06.2: SSAO pass skipped (kernel/noise not ready)");
		}

		// M06.3: SSAO blur
		std::fprintf(stderr, "[PIPELINE] 5 SSAO blur\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> blurVert = loadSpirv("shaders/ssao_blur.vert.spv");
			std::vector<uint32_t> blurFrag = loadSpirv("shaders/ssao_blur.frag.spv");
			if (!blurVert.empty() && !blurFrag.empty())
			{
				if (m_ssaoBlurPass.Init(device, physicalDevice, VK_FORMAT_R16_SFLOAT,
						blurVert.data(), blurVert.size(), blurFrag.data(), blurFrag.size(), 2))
					LOG_INFO(Render, "M06.3: SSAO bilateral blur pass ready");
				else
					LOG_WARN(Render, "M06.3: SSAO blur pass init failed");
			}
			else
				LOG_WARN(Render, "M06.3: SSAO blur shaders not found — blur disabled");
		}

		// Geometry pass
		std::fprintf(stderr, "[PIPELINE] 6 GeometryPass\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> vertSpirv = loadSpirv("shaders/gbuffer_geometry.vert.spv");
			std::vector<uint32_t> fragSpirv = loadSpirv("shaders/gbuffer_geometry.frag.spv");
			if (!vertSpirv.empty() && !fragSpirv.empty())
			{
				if (m_geometryPass.Init(device, physicalDevice,
						VK_FORMAT_R8G8B8A8_SRGB,
						VK_FORMAT_A2B10G10R10_UNORM_PACK32,
						VK_FORMAT_R8G8B8A8_UNORM,
						VK_FORMAT_R16G16_SFLOAT,
						VK_FORMAT_D32_SFLOAT,
						vertSpirv.data(), vertSpirv.size(),
						fragSpirv.data(), fragSpirv.size()))
					LOG_INFO(Render, "[Boot] DeferredPipeline GeometryPass OK");
				else
					LOG_WARN(Render, "[Boot] DeferredPipeline GeometryPass init failed");
			}
			else
				LOG_WARN(Render, "[Boot] DeferredPipeline GeometryPass shaders not found");
		}

		// Shadow map pass
		std::fprintf(stderr, "[PIPELINE] 7 ShadowMapPass\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> smVert = loadSpirv("shaders/shadow_depth.vert.spv");
			std::vector<uint32_t> smFrag = loadSpirv("shaders/shadow_depth.frag.spv");
			if (!smVert.empty() && !smFrag.empty())
			{
				if (m_shadowMapPass.Init(device, physicalDevice, VK_FORMAT_D32_SFLOAT, shadowMapResolution,
						smVert.data(), smVert.size(), smFrag.data(), smFrag.size()))
					LOG_INFO(Render, "[Boot] DeferredPipeline ShadowMapPass OK");
				else
					LOG_WARN(Render, "M04.2: shadow map pass init failed — disabled");
			}
			else
				LOG_WARN(Render, "M04.2: shadow map shaders not found — shadow pass disabled");
		}

		// Lighting pass
		std::fprintf(stderr, "[PIPELINE] 8 LightingPass\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> litVert = loadSpirv("shaders/lighting.vert.spv");
			std::vector<uint32_t> litFrag = loadSpirv("shaders/lighting.frag.spv");
			if (!litVert.empty() && !litFrag.empty())
			{
				if (m_lightingPass.Init(device, physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
						litVert.data(), litVert.size(), litFrag.data(), litFrag.size(), 2u))
					LOG_INFO(Render, "[Boot] DeferredPipeline LightingPass OK");
				else
					LOG_WARN(Render, "M03.2: lighting pass init failed — disabled");
			}
			else
				LOG_WARN(Render, "M03.2: lighting shaders not found — lighting pass disabled");
		}

		// Tonemap pass
		std::fprintf(stderr, "[PIPELINE] 9 TonemapPass\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> tmVert = loadSpirv("shaders/tonemap.vert.spv");
			std::vector<uint32_t> tmFrag = loadSpirv("shaders/tonemap.frag.spv");
			if (!tmVert.empty() && !tmFrag.empty())
			{
				if (m_tonemapPass.Init(device, physicalDevice, sceneColorLDRFormat,
						tmVert.data(), tmVert.size(), tmFrag.data(), tmFrag.size(), 2u))
					LOG_INFO(Render, "[Boot] DeferredPipeline TonemapPass OK");
				else
					LOG_WARN(Render, "M03.4: tonemap pass init failed — disabled");
			}
			else
				LOG_WARN(Render, "M03.4: tonemap shaders not found — tonemap pass disabled");
		}

		// Bloom prefilter + downsample
		std::fprintf(stderr, "[PIPELINE] 10 Bloom prefilter+downsample\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> bpVert = loadSpirv("shaders/bloom_prefilter.vert.spv");
			std::vector<uint32_t> bpFrag = loadSpirv("shaders/bloom_prefilter.frag.spv");
			std::vector<uint32_t> bdVert = loadSpirv("shaders/bloom_downsample.vert.spv");
			std::vector<uint32_t> bdFrag = loadSpirv("shaders/bloom_downsample.frag.spv");
			const VkFormat bloomFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
			if (!bpVert.empty() && !bpFrag.empty())
				m_bloomPrefilterPass.Init(device, physicalDevice, bloomFmt,
					bpVert.data(), bpVert.size(), bpFrag.data(), bpFrag.size(), 2u);
			if (!bdVert.empty() && !bdFrag.empty())
				m_bloomDownsamplePass.Init(device, physicalDevice, bloomFmt,
					bdVert.data(), bdVert.size(), bdFrag.data(), bdFrag.size(), 2u);
		}

		// Bloom upsample + combine
		std::fprintf(stderr, "[PIPELINE] 11 Bloom upsample+combine\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> buVert = loadSpirv("shaders/bloom_upsample.vert.spv");
			std::vector<uint32_t> buFrag = loadSpirv("shaders/bloom_upsample.frag.spv");
			std::vector<uint32_t> bcVert = loadSpirv("shaders/bloom_combine.vert.spv");
			std::vector<uint32_t> bcFrag = loadSpirv("shaders/bloom_combine.frag.spv");
			const VkFormat bloomFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
			if (!buVert.empty() && !buFrag.empty())
				m_bloomUpsamplePass.Init(device, physicalDevice, bloomFmt,
					buVert.data(), buVert.size(), buFrag.data(), buFrag.size(), 2u);
			if (!bcVert.empty() && !bcFrag.empty())
				m_bloomCombinePass.Init(device, physicalDevice, bloomFmt,
					bcVert.data(), bcVert.size(), bcFrag.data(), bcFrag.size(), 2u);
		}

		// M08.3: Auto-exposure
		std::fprintf(stderr, "[PIPELINE] 12 AutoExposure\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> lumComp = loadSpirv("shaders/luminance_reduce.comp.spv");
			if (!lumComp.empty())
			{
				if (m_autoExposure.Init(device, physicalDevice, vmaAllocator, lumComp.data(), lumComp.size()))
					LOG_INFO(Render, "M08.3: Auto-exposure ready");
			}
		}

		// M07.4: TAA
		std::fprintf(stderr, "[PIPELINE] 13 TAA\n"); std::fflush(stderr);
		{
			std::vector<uint32_t> taaVert = loadSpirv("shaders/taa.vert.spv");
			std::vector<uint32_t> taaFrag = loadSpirv("shaders/taa.frag.spv");
			if (!taaVert.empty() && !taaFrag.empty())
			{
				if (m_taaPass.Init(device, physicalDevice, VK_FORMAT_R8G8B8A8_UNORM,
						taaVert.data(), taaVert.size(), taaFrag.data(), taaFrag.size(), 2u))
					LOG_INFO(Render, "[Boot] DeferredPipeline TAA OK");
				else
					LOG_WARN(Render, "M07.4: TAA pass init failed");
			}
			else
				LOG_WARN(Render, "M07.4: TAA shaders not found — TAA disabled");
		}

		std::fprintf(stderr, "[PIPELINE] done\n"); std::fflush(stderr);
		LOG_INFO(Render, "[Boot] DeferredPipeline all passes init done");
		return true;
	}

	void DeferredPipeline::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		// Reverse init order: TAA → auto-exposure → bloom → tonemap → lighting → shadow → geometry → SSAO → specular/BRDF.
		m_taaPass.Destroy(device);
		m_autoExposure.Destroy(device);
		m_bloomCombinePass.Destroy(device);
		m_bloomUpsamplePass.Destroy(device);
		m_bloomDownsamplePass.Destroy(device);
		m_bloomPrefilterPass.Destroy(device);
		m_tonemapPass.Destroy(device);
		m_lightingPass.Destroy(device);
		m_shadowMapPass.Destroy(device);
		m_geometryPass.Destroy(device);
		m_ssaoBlurPass.Destroy(device);
		m_ssaoPass.Destroy(device);
		m_ssaoKernelNoise.Destroy(device);
		m_specularPrefilterPass.Destroy(device);
		m_brdfLutPass.Destroy(device);
	}

	void DeferredPipeline::InvalidateFramebufferCaches(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		m_geometryPass.InvalidateFramebufferCache(device);
		m_shadowMapPass.InvalidateFramebufferCache(device);
	}
}
