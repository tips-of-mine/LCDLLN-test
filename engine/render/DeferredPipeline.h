#pragma once

#include "engine/render/GeometryPass.h"
#include "engine/render/ShadowMapPass.h"
#include "engine/render/BrdfLutPass.h"
#include "engine/render/SpecularPrefilterPass.h"
#include "engine/render/SsaoKernelNoise.h"
#include "engine/render/SsaoPass.h"
#include "engine/render/SsaoBlurPass.h"
#include "engine/render/DecalPass.h"
#include "engine/render/LightingPass.h"
#include "engine/render/TonemapPass.h"
#include "engine/render/BloomPass.h"
#include "engine/render/AutoExposure.h"
#include "engine/render/TaaPass.h"

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
		ShadowMapPass         m_shadowMapPass;
		BrdfLutPass           m_brdfLutPass;
		SpecularPrefilterPass m_specularPrefilterPass;
		SsaoKernelNoise       m_ssaoKernelNoise;
		SsaoPass              m_ssaoPass;
		SsaoBlurPass          m_ssaoBlurPass;
		DecalPass             m_decalPass;
		LightingPass          m_lightingPass;
		TonemapPass           m_tonemapPass;
		BloomPrefilterPass    m_bloomPrefilterPass;
		BloomDownsamplePass   m_bloomDownsamplePass;
		BloomUpsamplePass     m_bloomUpsamplePass;
		BloomCombinePass      m_bloomCombinePass;
		AutoExposure          m_autoExposure;
		TaaPass               m_taaPass;
	};
}
