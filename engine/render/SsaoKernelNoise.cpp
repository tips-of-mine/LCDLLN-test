#include "engine/render/SsaoKernelNoise.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace engine::render
{
	namespace
	{
		/// Deterministic LCG for repeatable kernel/noise (constant between frames).
		float RandFloat(uint32_t& state)
		{
			state = state * 1103515245u + 12345u;
			return static_cast<float>(state & 0x7FFFfu) / 32768.0f;
		}

		uint16_t FloatToHalf(float f)
		{
			uint32_t u;
			std::memcpy(&u, &f, sizeof(float));
			uint32_t sign = (u >> 16) & 0x8000u;
			int32_t exp = static_cast<int32_t>((u >> 23) & 0xFFu) - 127;
			uint32_t mant = u & 0x7FFFFFu;
			if (exp <= -14) return static_cast<uint16_t>(sign);
			if (exp >= 15) return static_cast<uint16_t>(sign | 0x7C00u);
			uint32_t halfExp = static_cast<uint32_t>(exp + 15) << 10;
			return static_cast<uint16_t>(sign | halfExp | (mant >> 13));
		}
	}

	// UBO layout: 32 * vec3 (std140 = 16 bytes each) = 512, then radius (4), bias (4) = 520; round to 528.
	static constexpr size_t kKernelUboSize = 528u;

	bool SsaoKernelNoise::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		void* vmaAllocator,
		const engine::core::Config& config,
		VkQueue queue, uint32_t queueFamilyIndex)
	{
		std::fprintf(stderr, "[SSAO] Init enter vma=%p\n", vmaAllocator); std::fflush(stderr);
		
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || vmaAllocator == nullptr)
		{
			LOG_ERROR(Render, "SsaoKernelNoise::Init: invalid device");
			return false;
		}

		std::fprintf(stderr, "[SSAO] avant kernel gen\n"); std::fflush(stderr);
		
		// Radius and bias from config, clamped (notes: clamper radius/bias via config).
		float radius = static_cast<float>(config.GetDouble("ssao.radius", 0.5));
		float bias   = static_cast<float>(config.GetDouble("ssao.bias", 0.025));
		radius = (radius < 0.01f) ? 0.01f : (radius > 2.0f ? 2.0f : radius);
		bias   = (bias < 0.0f) ? 0.0f : (bias > 0.1f ? 0.1f : bias);

		// ---- Generate kernel on CPU (hemisphere, biased toward centre) ----
		float kernelData[32 * 4]; // vec3 + pad per slot for std140
		uint32_t rng = 42u;
		for (uint32_t i = 0; i < kKernelSize; ++i)
		{
			float x = RandFloat(rng) * 2.0f - 1.0f;
			float y = RandFloat(rng) * 2.0f - 1.0f;
			float z = RandFloat(rng);
			if (x * x + y * y + z * z > 1.0f)
			{
				--i;
				continue;
			}
			float len = std::sqrt(x * x + y * y + z * z);
			x /= len;
			y /= len;
			z /= len;
			float t = static_cast<float>(i) / static_cast<float>(kKernelSize);
			float scale = 0.1f + 0.9f * t * t;
			kernelData[i * 4 + 0] = x * scale;
			kernelData[i * 4 + 1] = y * scale;
			kernelData[i * 4 + 2] = z * scale;
			kernelData[i * 4 + 3] = 0.0f;
		}

		std::fprintf(stderr, "[SSAO] kernel gen OK\n"); std::fflush(stderr);

		std::fprintf(stderr, "[SSAO] avant vmaCreateBuffer kernel\n"); std::fflush(stderr);
		m_vmaAllocator = vmaAllocator;
		VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);

		// ---- Create kernel UBO (host-visible for upload) ----
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size  = kKernelUboSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		VmaAllocationCreateInfo allocCreateInfo{};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

		std::fprintf(stderr, "[SSAO] avant vmaCreateBuffer kernel alloc=%p\n", (void*)alloc); std::fflush(stderr);
		VmaAllocation kernelAlloc = VK_NULL_HANDLE;
		VkResult vmaRes = vmaCreateBuffer(alloc, &bufInfo, &allocCreateInfo, &m_kernelBuffer, &kernelAlloc, nullptr);
		std::fprintf(stderr, "[SSAO] vmaCreateBuffer kernel result=%d buf=%p\n", (int)vmaRes, (void*)m_kernelBuffer); std::fflush(stderr);
		if (vmaRes != VK_SUCCESS)
		{
		    LOG_ERROR(Render, "SsaoKernelNoise: vmaCreateBuffer (kernel UBO) failed");
		    return false;
		}
		std::fprintf(stderr, "[SSAO] avant vmaMapMemory\n"); std::fflush(stderr);
		m_kernelAlloc = kernelAlloc;
		void* mapped = nullptr;
		if (vmaMapMemory(alloc, kernelAlloc, &mapped) != VK_SUCCESS)
		{
		    std::fprintf(stderr, "[SSAO] vmaMapMemory FAILED\n"); std::fflush(stderr);
		    Destroy(device);
		    return false;
		}
		std::fprintf(stderr, "[SSAO] vmaMapMemory OK mapped=%p\n", mapped); std::fflush(stderr);

		std::fprintf(stderr, "[SSAO] avant memcpy kernel\n"); std::fflush(stderr);
		std::memcpy(mapped, kernelData, 32u * 4u * sizeof(float));
		std::fprintf(stderr, "[SSAO] memcpy kernel OK\n"); std::fflush(stderr);
		
		std::memcpy(static_cast<char*>(mapped) + 512, &radius, sizeof(float));
		std::memcpy(static_cast<char*>(mapped) + 516, &bias, sizeof(float));
		vmaUnmapMemory(alloc, kernelAlloc);

		// ---- Generate 4x4 noise (RG16F: random XY for TBN rotation) ----
		uint16_t noisePixels[4 * 4 * 2];
		for (uint32_t i = 0; i < 16u; ++i)
		{
			float nx = RandFloat(rng) * 2.0f - 1.0f;
			float ny = RandFloat(rng) * 2.0f - 1.0f;
			noisePixels[i * 2 + 0] = FloatToHalf(nx);
			noisePixels[i * 2 + 1] = FloatToHalf(ny);
		}

		// ---- Create noise image (4x4 RG16F) ----
		VkImageCreateInfo imgInfo{};
		imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType     = VK_IMAGE_TYPE_2D;
		imgInfo.format        = VK_FORMAT_R16G16_SFLOAT;
		imgInfo.extent        = { kNoiseSize, kNoiseSize, 1 };
		imgInfo.mipLevels      = 1;
		imgInfo.arrayLayers   = 1;
		imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		VmaAllocation noiseAlloc = VK_NULL_HANDLE;
		if (vmaCreateImage(alloc, &imgInfo, &imgAllocInfo, &m_noiseImage, &noiseAlloc, nullptr) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vmaCreateImage (noise) failed");
			Destroy(device);
			return false;
		}
		m_noiseAlloc = noiseAlloc;

		// ---- Staging buffer + upload noise ----
		VkBuffer stagingBuf = VK_NULL_HANDLE;
		VmaAllocation stagingAlloc = VK_NULL_HANDLE;
		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size  = 64u;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		if (vmaCreateBuffer(alloc, &stagingInfo, &allocCreateInfo, &stagingBuf, &stagingAlloc, nullptr) != VK_SUCCESS)
		{
			Destroy(device);
			return false;
		}
		if (vmaMapMemory(alloc, stagingAlloc, &mapped) != VK_SUCCESS)
		{
			vmaDestroyBuffer(alloc, stagingBuf, stagingAlloc);
			Destroy(device);
			return false;
		}
		std::memcpy(mapped, noisePixels, 64);
		vmaUnmapMemory(alloc, stagingAlloc);

		// One-time copy: command pool + buffer, transition image, copy, transition to SHADER_READ_ONLY.
		if (queue != VK_NULL_HANDLE)
		{
			VkCommandPool cmdPool = VK_NULL_HANDLE;
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = queueFamilyIndex;
			poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			if (vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool) == VK_SUCCESS)
			{
				VkCommandBuffer cmd = VK_NULL_HANDLE;
				VkCommandBufferAllocateInfo allocCmd{};
				allocCmd.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocCmd.commandPool        = cmdPool;
				allocCmd.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocCmd.commandBufferCount = 1;
				if (vkAllocateCommandBuffers(device, &allocCmd, &cmd) == VK_SUCCESS)
				{
					VkCommandBufferBeginInfo beginInfo{};
					beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
					beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
					if (vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS)
					{
						VkImageMemoryBarrier barrier{};
						barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
						barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.image               = m_noiseImage;
						barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
						vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

						VkBufferImageCopy region{};
						region.bufferOffset      = 0;
						region.bufferRowLength   = kNoiseSize;
						region.bufferImageHeight = kNoiseSize;
						region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
						region.imageOffset       = { 0, 0, 0 };
						region.imageExtent       = { kNoiseSize, kNoiseSize, 1 };
						vkCmdCopyBufferToImage(cmd, stagingBuf, m_noiseImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

						barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
						vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

						vkEndCommandBuffer(cmd);

						VkSubmitInfo submit{};
						submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
						submit.commandBufferCount = 1;
						submit.pCommandBuffers    = &cmd;
						if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS)
							vkQueueWaitIdle(queue);
					}
					vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
				}
				vkDestroyCommandPool(device, cmdPool, nullptr);
			}
		}
		else
		{
			// No queue: leave image in UNDEFINED; view/sampler still created for later upload.
		}

		vmaDestroyBuffer(alloc, stagingBuf, stagingAlloc);

		// ---- Image view (4x4 RG16F) ----
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image    = m_noiseImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format   = VK_FORMAT_R16G16_SFLOAT;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_noiseView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vkCreateImageView (noise) failed");
			Destroy(device);
			return false;
		}

		// ---- Sampler (tiled = repeat) ----
		VkSamplerCreateInfo sampInfo{};
		sampInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampInfo.magFilter     = VK_FILTER_NEAREST;
		sampInfo.minFilter     = VK_FILTER_NEAREST;
		sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampInfo.maxLod        = 0.0f;
		if (vkCreateSampler(device, &sampInfo, nullptr, &m_noiseSampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vkCreateSampler (noise) failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "SsaoKernelNoise: kernel UBO + 4x4 noise texture ready (radius={}, bias={})", radius, bias);
		return true;
	}

	void SsaoKernelNoise::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;
		if (m_vmaAllocator == nullptr) return;
		VmaAllocator alloc = static_cast<VmaAllocator>(m_vmaAllocator);
		if (m_noiseSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_noiseSampler, nullptr);
			m_noiseSampler = VK_NULL_HANDLE;
		}
		if (m_noiseView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_noiseView, nullptr);
			m_noiseView = VK_NULL_HANDLE;
		}
		if (m_noiseImage != VK_NULL_HANDLE && m_noiseAlloc != nullptr)
		{
			vmaDestroyImage(alloc, m_noiseImage, static_cast<VmaAllocation>(m_noiseAlloc));
			m_noiseImage = VK_NULL_HANDLE;
			m_noiseAlloc = nullptr;
		}
		if (m_kernelBuffer != VK_NULL_HANDLE && m_kernelAlloc != nullptr)
		{
			vmaDestroyBuffer(alloc, m_kernelBuffer, static_cast<VmaAllocation>(m_kernelAlloc));
			m_kernelBuffer = VK_NULL_HANDLE;
			m_kernelAlloc = nullptr;
		}
		m_vmaAllocator = nullptr;
	}
}
