#include "engine/render/SsaoKernelNoise.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>
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
		LOG_INFO(Render, "[SSAO] Init enter vma={}", vmaAllocator);
		
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "SsaoKernelNoise::Init: invalid device");
			return false;
		}

		LOG_DEBUG(Render, "[SSAO] avant kernel ge");
		
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

		LOG_INFO(Render, "[SSAO] kernel gen OK");

		LOG_DEBUG(Render, "[SSAO] avant createBuffer kernel");
		m_vmaAllocator = vmaAllocator; // conservé pour compat, non utilisé pour l'alloc actuelle.

		// Helpers mémoire
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

		auto findMemoryType = [&](uint32_t typeBits, VkMemoryPropertyFlags desiredFlags) -> uint32_t
		{
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeBits & (1u << i)) &&
					(memProps.memoryTypes[i].propertyFlags & desiredFlags) == desiredFlags)
				{
					return i;
				}
			}
			return UINT32_MAX;
		};

		auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags memFlags,
			VkBuffer& outBuffer, void*& outAlloc) -> bool
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = size;
			bufInfo.usage = usage;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(device, &bufInfo, nullptr, &outBuffer) != VK_SUCCESS || outBuffer == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "SsaoKernelNoise: vkCreateBuffer failed (usage=0x{:x})", usage);
				return false;
			}

			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
			uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits, memFlags);
			if (memTypeIdx == UINT32_MAX)
			{
				LOG_ERROR(Render, "SsaoKernelNoise: no suitable memory type (flags=0x{:x})", memFlags);
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIdx;

			VkDeviceMemory mem = VK_NULL_HANDLE;
			if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS || mem == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "SsaoKernelNoise: vkAllocateMemory failed");
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			if (vkBindBufferMemory(device, outBuffer, mem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "SsaoKernelNoise: vkBindBufferMemory failed");
				vkFreeMemory(device, mem, nullptr);
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			outAlloc = reinterpret_cast<void*>(mem);
			return true;
		};

		// ---- Create kernel UBO (host-visible for upload) ----
		if (!createBuffer(kKernelUboSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				m_kernelBuffer, m_kernelAlloc))
		{
			return false;
		}
		void* mapped = nullptr;
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_kernelAlloc);
			if (vkMapMemory(device, mem, 0, kKernelUboSize, 0, &mapped) != VK_SUCCESS)
			{
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "[SSAO] avant memcpy kernel");
		std::memcpy(mapped, kernelData, 32u * 4u * sizeof(float));
		LOG_INFO(Render, "[SSAO] memcpy kernel OK");
		
		std::memcpy(static_cast<char*>(mapped) + 512, &radius, sizeof(float));
		std::memcpy(static_cast<char*>(mapped) + 516, &bias, sizeof(float));
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_kernelAlloc);
			vkUnmapMemory(device, mem);
		}

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
		if (vkCreateImage(device, &imgInfo, nullptr, &m_noiseImage) != VK_SUCCESS || m_noiseImage == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vkCreateImage (noise) failed");
			Destroy(device);
			return false;
		}

		VkMemoryRequirements imgMemReq{};
		vkGetImageMemoryRequirements(device, m_noiseImage, &imgMemReq);
		uint32_t imgMemTypeIdx = findMemoryType(imgMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (imgMemTypeIdx == UINT32_MAX)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: no suitable DEVICE_LOCAL memory type for noise image");
			Destroy(device);
			return false;
		}

		VkMemoryAllocateInfo imgAllocInfo{};
		imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		imgAllocInfo.allocationSize = imgMemReq.size;
		imgAllocInfo.memoryTypeIndex = imgMemTypeIdx;
		VkDeviceMemory noiseMem = VK_NULL_HANDLE;
		if (vkAllocateMemory(device, &imgAllocInfo, nullptr, &noiseMem) != VK_SUCCESS || noiseMem == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vkAllocateMemory (noise image) failed");
			Destroy(device);
			return false;
		}
		if (vkBindImageMemory(device, m_noiseImage, noiseMem, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "SsaoKernelNoise: vkBindImageMemory (noise) failed");
			vkFreeMemory(device, noiseMem, nullptr);
			Destroy(device);
			return false;
		}
		m_noiseAlloc = reinterpret_cast<void*>(noiseMem);

		// ---- Staging buffer + upload noise ----
		VkBuffer stagingBuf = VK_NULL_HANDLE;
		void* stagingMemVoid = nullptr;
		if (!createBuffer(64u,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				stagingBuf, stagingMemVoid))
		{
			Destroy(device);
			return false;
		}
		VkDeviceMemory stagingMem = reinterpret_cast<VkDeviceMemory>(stagingMemVoid);
		if (vkMapMemory(device, stagingMem, 0, 64u, 0, &mapped) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, stagingBuf, nullptr);
			vkFreeMemory(device, stagingMem, nullptr);
			Destroy(device);
			return false;
		}
		std::memcpy(mapped, noisePixels, 64);
		vkUnmapMemory(device, stagingMem);

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

		vkDestroyBuffer(device, stagingBuf, nullptr);
		vkFreeMemory(device, stagingMem, nullptr);

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
		LOG_DEBUG(Render, "[SSAO] Destroy enter");
		if (device == VK_NULL_HANDLE) return;
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
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_noiseAlloc);
			vkDestroyImage(device, m_noiseImage, nullptr);
			vkFreeMemory(device, mem, nullptr);
			m_noiseImage = VK_NULL_HANDLE;
			m_noiseAlloc = nullptr;
		}
		if (m_kernelBuffer != VK_NULL_HANDLE && m_kernelAlloc != nullptr)
		{
			VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(m_kernelAlloc);
			vkDestroyBuffer(device, m_kernelBuffer, nullptr);
			vkFreeMemory(device, mem, nullptr);
			m_kernelBuffer = VK_NULL_HANDLE;
			m_kernelAlloc = nullptr;
		}
		m_vmaAllocator = nullptr;
		LOG_INFO(Render, "[SSAO] Destroy OK");
	}
}
