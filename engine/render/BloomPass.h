#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Bloom prefilter pass (M08.1): samples SceneColor_HDR, applies soft threshold (threshold + knee),
	/// writes BloomMip0 (HDR). Fullscreen triangle, one combined image sampler, push constants (threshold, knee).
	class BloomPrefilterPass
	{
	public:
		struct PrefilterParams
		{
			float threshold; ///< Luminance threshold (default 1.0).
			float knee;      ///< Soft transition (default 0.5).
		};

		BloomPrefilterPass() = default;
		BloomPrefilterPass(const BloomPrefilterPass&) = delete;
		BloomPrefilterPass& operator=(const BloomPrefilterPass&) = delete;

		/// Creates render pass, descriptor set layout, pool, sampler, pipeline.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat bloomFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records prefilter: read idSceneColorHDR, write idBloomMip0. Extent = full resolution.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorHDR,
			ResourceId idBloomMip0,
			const PrefilterParams& params, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout     m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};

	/// Bloom downsample pass (M08.1): reads one mip, writes next (box 2x2). Fullscreen triangle.
	class BloomDownsamplePass
	{
	public:
		BloomDownsamplePass() = default;
		BloomDownsamplePass(const BloomDownsamplePass&) = delete;
		BloomDownsamplePass& operator=(const BloomDownsamplePass&) = delete;

		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat bloomFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records downsample: read idSrc (larger mip), write idDst (smaller). extentDst = output size.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extentDst,
			ResourceId idSrc,
			ResourceId idDst,
			uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout     m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};

	/// Bloom upsample pass (M08.2): samples smaller mip (bilinear), additive blend into larger mip.
	/// Order: from smallest (Mip5) toward Mip0; each pass adds upsampled(Mip_{i+1}) to Mip_i.
	class BloomUpsamplePass
	{
	public:
		BloomUpsamplePass() = default;
		BloomUpsamplePass(const BloomUpsamplePass&) = delete;
		BloomUpsamplePass& operator=(const BloomUpsamplePass&) = delete;

		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat bloomFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records upsample: read idSrc (smaller mip), add into idDst (larger mip). extentDst = output size. Uses LOAD + additive blend.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extentDst,
			ResourceId idSrc,
			ResourceId idDst,
			uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};

	/// Bloom combine pass (M08.2): SceneColor_HDR + bloom * intensity → HDR output (before tonemap).
	class BloomCombinePass
	{
	public:
		struct CombineParams
		{
			float intensity; ///< Bloom intensity multiplier (config: bloom.intensity).
		};

		BloomCombinePass() = default;
		BloomCombinePass(const BloomCombinePass&) = delete;
		BloomCombinePass& operator=(const BloomCombinePass&) = delete;

		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat hdrFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2);

		/// Records combine: read idSceneColorHDR and idBloom, write idSceneColorHDRWithBloom.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorHDR,
			ResourceId idBloom,
			ResourceId idSceneColorHDRWithBloom,
			const CombineParams& params, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};
}
