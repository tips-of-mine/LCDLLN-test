#pragma once

#include "engine/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// Auto-exposure (M08.3): compute log-average luminance from HDR, temporal adaptation,
	/// persistent exposure buffer. Exposure = lerp(prev, target, 1-exp(-dt*speed)); key=0.18.
	class AutoExposure
	{
	public:
		/// Grid size for luminance sampling (e.g. 64 -> 64*64 samples).
		static constexpr uint32_t kLuminanceGridSize = 64;
		static constexpr uint32_t kLuminanceSampleCount = kLuminanceGridSize * kLuminanceGridSize;

		AutoExposure() = default;
		AutoExposure(const AutoExposure&) = delete;
		AutoExposure& operator=(const AutoExposure&) = delete;

		/// Creates compute pipeline, luminance buffer, staging buffer, exposure buffer.
		/// vmaAllocator = centralised GPU allocator (VMA); cast to VmaAllocator in implementation.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			const uint32_t* compSpirv, size_t compWordCount);

		/// Records compute pass: sample HDR -> log(L) per pixel into luminance buffer; then copies to staging.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			ResourceId idSceneColorHDR,
			VkExtent2D extent);

		/// Call after fence wait (staging has last frame's data). Reads staging, computes log-avg, adapts exposure, writes exposure buffer.
		/// key = target mid-gray (default 0.18); speed = adaptation speed.
		void Update(VkDevice device, float dt, float key, float speed);

		/// Returns current exposure for tonemap (valid after first Update).
		float GetExposure() const { return m_exposure; }

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

		/// Exposure buffer (1 float, host-visible). For binding as uniform if needed.
		VkBuffer GetExposureBuffer() const { return m_exposureBuffer; }

	private:
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkDescriptorSet       m_descriptorSet        = VK_NULL_HANDLE;
		VkPipelineLayout     m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sampler             = VK_NULL_HANDLE;

		void*   m_vmaAllocator     = nullptr;
		VkBuffer m_luminanceBuffer   = VK_NULL_HANDLE;
		void*   m_luminanceAlloc   = nullptr; ///< VmaAllocation
		VkBuffer m_stagingBuffer     = VK_NULL_HANDLE;
		void*   m_stagingAlloc     = nullptr; ///< VmaAllocation
		VkBuffer m_exposureBuffer    = VK_NULL_HANDLE;
		void*   m_exposureAlloc    = nullptr; ///< VmaAllocation

		float m_exposure = 1.0f;
	};

} // namespace engine::render
