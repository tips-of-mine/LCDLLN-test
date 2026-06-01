#pragma once

#include "src/client/render/GeometryPass.h"
#include "src/client/render/GpuDrivenCullingPass.h"
#include "src/client/render/HiZPyramidPass.h"
#include "src/client/render/Material.h"
#include "src/client/render/ShadowMapPass.h"
#include "src/client/render/BrdfLutPass.h"
#include "src/client/render/SpecularPrefilterPass.h"
#include "src/client/render/SsaoKernelNoise.h"
#include "src/client/render/SsaoPass.h"
#include "src/client/render/SsaoBlurPass.h"
#include "src/client/render/DecalPass.h"
#include "src/client/render/LightingPass.h"
#include "src/client/render/VolumetricFogPass.h"
#include "src/client/render/DepthOfFieldPass.h"
#include "src/client/render/ImpostorPass.h"
#include "src/client/render/TonemapPass.h"
#include "src/client/render/BloomPass.h"
#include "src/client/render/AutoExposure.h"
#include "src/client/render/TaaPass.h"
#include "src/client/render/PipelineCache.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace engine::core
{
	class Config;
}

namespace engine::render
{
	/// Encapsulates all deferred rendering passes (geometry, shadow, SSAO, lighting, bloom, tonemap, TAA).
	/// Init/Destroy centralise pass lifecycle; Engine still registers frame-graph passes and calls Record via getters.
	class DeferredPipeline
	{
	public:
		using ShaderLoaderFn = std::function<std::vector<uint32_t>(const char* spvPath)>;

		DeferredPipeline() = default;
		DeferredPipeline(const DeferredPipeline&) = delete;
		DeferredPipeline& operator=(const DeferredPipeline&) = delete;

		/// Initialises all passes (BRDF LUT, specular prefilter, SSAO kernel/noise/pass/blur,
		/// geometry, shadow, lighting, tonemap, bloom chain, auto-exposure, TAA).
		/// \param loadSpirv  Returns SPIR-V words for a given path (e.g. "shaders/foo.vert.spv"); may compile on the fly.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			const engine::core::Config& config,
			uint32_t shadowMapResolution,
			VkFormat sceneColorLDRFormat,
			VkQueue graphicsQueue,
			uint32_t graphicsQueueFamilyIndex,
			ShaderLoaderFn loadSpirv);

		/// Destroys all passes in reverse init order. Safe to call when not initialised.
		void Destroy(VkDevice device);

		/// Destroys cached framebuffers in geometry and shadow passes (call on resize before FG destroy).
		void InvalidateFramebufferCaches(VkDevice device);

		GeometryPass&         GetGeometryPass()         { return m_geometryPass; }
		const GeometryPass&   GetGeometryPass() const   { return m_geometryPass; }
		MaterialDescriptorCache& GetMaterialDescriptorCache() { return m_materialDescriptorCache; }
		const MaterialDescriptorCache& GetMaterialDescriptorCache() const { return m_materialDescriptorCache; }
		GpuDrivenCullingPass& GetGpuDrivenCullingPass() { return m_gpuDrivenCullingPass; }
		const GpuDrivenCullingPass& GetGpuDrivenCullingPass() const { return m_gpuDrivenCullingPass; }
		HiZPyramidPass&       GetHiZPyramidPass()       { return m_hiZPyramidPass; }
		const HiZPyramidPass& GetHiZPyramidPass() const { return m_hiZPyramidPass; }
		ShadowMapPass&        GetShadowMapPass()        { return m_shadowMapPass; }
		const ShadowMapPass&  GetShadowMapPass() const  { return m_shadowMapPass; }
		BrdfLutPass&          GetBrdfLutPass()          { return m_brdfLutPass; }
		const BrdfLutPass&    GetBrdfLutPass() const    { return m_brdfLutPass; }
		SpecularPrefilterPass& GetSpecularPrefilterPass() { return m_specularPrefilterPass; }
		const SpecularPrefilterPass& GetSpecularPrefilterPass() const { return m_specularPrefilterPass; }
		SsaoKernelNoise&      GetSsaoKernelNoise()      { return m_ssaoKernelNoise; }
		const SsaoKernelNoise& GetSsaoKernelNoise() const { return m_ssaoKernelNoise; }
		SsaoPass&             GetSsaoPass()             { return m_ssaoPass; }
		const SsaoPass&       GetSsaoPass() const       { return m_ssaoPass; }
		SsaoBlurPass&         GetSsaoBlurPass()         { return m_ssaoBlurPass; }
		const SsaoBlurPass&   GetSsaoBlurPass() const   { return m_ssaoBlurPass; }
		DecalPass&            GetDecalPass()            { return m_decalPass; }
		const DecalPass&      GetDecalPass() const      { return m_decalPass; }
		LightingPass&         GetLightingPass()         { return m_lightingPass; }
		const LightingPass&  GetLightingPass() const    { return m_lightingPass; }
		VolumetricFogPass&        GetVolumetricFogPass()        { return m_volumetricFogPass; }
		const VolumetricFogPass&  GetVolumetricFogPass() const  { return m_volumetricFogPass; }
		DepthOfFieldPass&         GetDepthOfFieldPass()         { return m_depthOfFieldPass; }
		const DepthOfFieldPass&   GetDepthOfFieldPass() const   { return m_depthOfFieldPass; }
		ImpostorPass&             GetImpostorPass()             { return m_impostorPass; }
		const ImpostorPass&       GetImpostorPass() const       { return m_impostorPass; }
		TonemapPass&          GetTonemapPass()          { return m_tonemapPass; }
		const TonemapPass&    GetTonemapPass() const    { return m_tonemapPass; }
		BloomPrefilterPass&   GetBloomPrefilterPass()   { return m_bloomPrefilterPass; }
		BloomDownsamplePass&  GetBloomDownsamplePass()  { return m_bloomDownsamplePass; }
		BloomUpsamplePass&    GetBloomUpsamplePass()    { return m_bloomUpsamplePass; }
		BloomCombinePass&     GetBloomCombinePass()     { return m_bloomCombinePass; }
		AutoExposure&         GetAutoExposure()          { return m_autoExposure; }
		const AutoExposure&   GetAutoExposure() const   { return m_autoExposure; }
		TaaPass&              GetTaaPass()             { return m_taaPass; }
		const TaaPass&        GetTaaPass() const        { return m_taaPass; }

	private:
		GeometryPass          m_geometryPass;
		MaterialDescriptorCache m_materialDescriptorCache;
		GpuDrivenCullingPass  m_gpuDrivenCullingPass;
		HiZPyramidPass        m_hiZPyramidPass;
		ShadowMapPass         m_shadowMapPass;
		BrdfLutPass           m_brdfLutPass;
		SpecularPrefilterPass m_specularPrefilterPass;
		SsaoKernelNoise       m_ssaoKernelNoise;
		SsaoPass              m_ssaoPass;
		SsaoBlurPass          m_ssaoBlurPass;
		DecalPass             m_decalPass;
		LightingPass          m_lightingPass;
		VolumetricFogPass     m_volumetricFogPass;
		DepthOfFieldPass      m_depthOfFieldPass;
		ImpostorPass          m_impostorPass; ///< M45.5 — impostors végétation (gated world.impostor.enabled)
		TonemapPass           m_tonemapPass;
		BloomPrefilterPass    m_bloomPrefilterPass;
		BloomDownsamplePass   m_bloomDownsamplePass;
		BloomUpsamplePass     m_bloomUpsamplePass;
		BloomCombinePass      m_bloomCombinePass;
		AutoExposure          m_autoExposure;
		TaaPass               m_taaPass;
		PipelineCache        m_pipelineCache;
	};
}
