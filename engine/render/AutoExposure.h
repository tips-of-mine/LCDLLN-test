#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// Auto-exposure via luminance histogram (M08.6) with temporal adaptation.
	/// Exposure = lerp(prev, key / avgLuminance, 1-exp(-dt*speed)).
	class AutoExposure
	{
	public:
		static constexpr uint32_t kAESlots = 2;
		static constexpr uint32_t kHistogramBinCount = 256;

		AutoExposure() = default;
		AutoExposure(const AutoExposure&) = delete;
		AutoExposure& operator=(const AutoExposure&) = delete;

		/// Creates compute pipelines, per-slot histogram/staging buffers, and the exposure buffer.
		/// vmaAllocator est conservé pour compat (branches VMA), mais l'implémentation
		/// actuelle utilise du Vulkan brut pour les buffers.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			const uint32_t* histogramCompSpirv, size_t histogramCompWordCount,
			const uint32_t* histogramAvgCompSpirv, size_t histogramAvgCompWordCount,
			float histogramPercentileLow = 0.10f,
			float histogramPercentileHigh = 0.90f);

		/// Records the 2 histogram passes for the given frame slot:
		/// 1) reset + accumulate histogram from HDR,
		/// 2) compute percentile-filtered average log luminance into the slot staging buffer.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			ResourceId idSceneColorHDR,
			VkExtent2D extent,
			uint32_t frameIndex);

		/// Call after fence wait. Reads the previous slot staging buffer, computes log-avg,
		/// adapts exposure, writes exposure buffer.
		/// key = target mid-gray (default 0.18); speed = adaptation speed.
		void Update(VkDevice device, float dt, float key, float speed, uint32_t frameIndex);

		/// Returns current exposure for tonemap (valid after first Update).
		float GetExposure() const { return m_exposure; }

		void Destroy(VkDevice device);
		bool IsValid() const { return m_histogramPipeline != VK_NULL_HANDLE && m_averagePipeline != VK_NULL_HANDLE; }

		/// Exposure buffer (1 float, host-visible). For binding as uniform if needed.
		VkBuffer GetExposureBuffer() const { return m_exposureBuffer; }

	private:
		struct HistogramPushConstants
		{
			float percentileLow  = 0.10f;
			float percentileHigh = 0.90f;
			float logMin         = -12.0f;
			float logMax         = 4.0f;
		};

		VkDescriptorSetLayout m_histogramDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_averageDescriptorSetLayout   = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool               = VK_NULL_HANDLE;
		VkDescriptorSet       m_histogramDescriptorSets[kAESlots] = {};
		VkDescriptorSet       m_averageDescriptorSets[kAESlots]   = {};
		VkPipelineLayout      m_histogramPipelineLayout      = VK_NULL_HANDLE;
		VkPipelineLayout      m_averagePipelineLayout        = VK_NULL_HANDLE;
		VkPipeline            m_histogramPipeline            = VK_NULL_HANDLE;
		VkPipeline            m_averagePipeline              = VK_NULL_HANDLE;
		VkSampler             m_sampler                      = VK_NULL_HANDLE;

		void*        m_vmaAllocator   = nullptr;
		VkBuffer     m_histogramBuffer[kAESlots] = {};
		void*        m_histogramAlloc[kAESlots]  = {}; ///< stocke VkDeviceMemory (cast)
		VkBuffer     m_stagingBuffer[kAESlots]   = {};
		void*        m_stagingAlloc[kAESlots]    = {}; ///< stocke VkDeviceMemory (cast)
		VkBuffer     m_exposureBuffer  = VK_NULL_HANDLE;
		void*        m_exposureAlloc   = nullptr; ///< stocke VkDeviceMemory (cast)
		HistogramPushConstants m_histogramParams{};

		float m_exposure = 1.0f;

		static VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* spirv, size_t wordCount);
	};

} // namespace engine::render
