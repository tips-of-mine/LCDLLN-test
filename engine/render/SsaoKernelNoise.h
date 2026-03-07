#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::core
{
	class Config;
}

namespace engine::render
{
	/// M06.1: SSAO kernel (16–32 samples) + 4x4 noise texture for TBN rotation.
	///
	/// Kernel is generated on CPU at init (hemisphere samples biased toward centre),
	/// noise is a 4x4 RG16F texture (tiled/repeat). Both are uploaded once and remain
	/// constant between frames. Radius and bias are read from config and clamped.
	class SsaoKernelNoise
	{
	public:
		static constexpr uint32_t kKernelSize = 32u;
		static constexpr uint32_t kNoiseSize  = 4u;

		SsaoKernelNoise() = default;
		SsaoKernelNoise(const SsaoKernelNoise&) = delete;
		SsaoKernelNoise& operator=(const SsaoKernelNoise&) = delete;

		/// Generates kernel on CPU, creates UBO and 4x4 noise texture, uploads both.
		/// Reads and clamps ssao.radius and ssao.bias from config.
		/// \param vmaAllocator Centralised GPU allocator (VMA); cast to VmaAllocator in implementation.
		/// \param queue  Used for one-time staging copy of noise texture; can be graphics queue.
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			void* vmaAllocator,
			const engine::core::Config& config,
			VkQueue queue, uint32_t queueFamilyIndex);

		/// Releases UBO, noise image/view/sampler. Safe to call when not initialised.
		void Destroy(VkDevice device);

		/// UBO containing kernel samples (vec3[kKernelSize]) then radius (float), bias (float). std140 layout.
		VkBuffer GetKernelBuffer() const { return m_kernelBuffer; }

		/// Noise texture view (4x4 RG16F, tiled).
		VkImageView GetNoiseImageView() const { return m_noiseView; }

		/// Sampler for noise (repeat wrap, nearest for crisp tiling).
		VkSampler GetNoiseSampler() const { return m_noiseSampler; }

		bool IsValid() const { return m_kernelBuffer != VK_NULL_HANDLE && m_noiseView != VK_NULL_HANDLE; }

	private:
		void*          m_vmaAllocator = nullptr;
		VkBuffer       m_kernelBuffer = VK_NULL_HANDLE;
		void*          m_kernelAlloc  = nullptr; ///< VmaAllocation
		VkImage        m_noiseImage   = VK_NULL_HANDLE;
		void*          m_noiseAlloc   = nullptr; ///< VmaAllocation
		VkImageView    m_noiseView    = VK_NULL_HANDLE;
		VkSampler      m_noiseSampler = VK_NULL_HANDLE;
	};
}
