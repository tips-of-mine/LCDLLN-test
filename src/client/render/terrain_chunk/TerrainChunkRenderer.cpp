// src/client/render/terrain_chunk/TerrainChunkRenderer.cpp (Task 11 — M100)
//
// Implémentation de `TerrainChunkRenderer`. Voir TerrainChunkRenderer.h pour
// la documentation API. Ce fichier contient :
//   1. 3 allocateurs Vulkan concrets (Buffer / Image / ImageArray) qui
//      reproduisent le pattern staging buffer + barrier + copyBufferToImage
//      du legacy `src/client/render/terrain/TerrainSplatting.cpp`.
//   2. Le chargement boot-time des 24 textures PBR (8 layers × 3 maps) via
//      `stb_image::stbi_load` + résolution de path par `ResolveLayerAssetPath`.
//      Stratégie de fallback : magenta 1×1 si fichier absent ou taille
//      hétérogène vs layer 0.
//   3. Le LayerParams UBO 128 octets (std140 padding `vec4 tilingScale[8]`).
//   4. La boucle `RenderVisibleChunks` qui itère, lazy-uploade, écrit les 6
//      bindings du splat set via `vkUpdateDescriptorSets`, et drawe.
//   5. Le `Tick` : eviction LRU + reset descriptor pool.
//
// Pas de branche `m_editorEnabled` : critère M100.5/.9.

#include "src/client/render/terrain_chunk/TerrainChunkRenderer.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/client/render/AssetRegistry.h"
#include "src/client/render/vk/StagingAllocator.h"
#include "src/client/world/StreamCache.h"
#include "src/client/world/terrain/LayerPalette.h"
#include "src/client/world/terrain/SplatMap.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainMeshBuilder.h"

// stb_image: STB_IMAGE_IMPLEMENTATION est défini dans AssetRegistry.cpp.
// On inclut l'en-tête uniquement pour les déclarations de stbi_load /
// stbi_image_free.
#include "stb_image.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <vector>

namespace engine::render::terrain_chunk
{
	// ─────────────────────────────────────────────────────────────────────────
	// Helpers Vulkan partagés (statiques)
	// ─────────────────────────────────────────────────────────────────────────
	namespace
	{
		/// Cherche un memoryType compatible avec `typeBits` ET les flags
		/// `desired`. Identique au helper du legacy TerrainSplatting.
		uint32_t FindMemoryType(VkPhysicalDevice physDev, uint32_t typeBits,
			VkMemoryPropertyFlags desired)
		{
			VkPhysicalDeviceMemoryProperties props{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &props);
			for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
			{
				if ((typeBits & (1u << i))
					&& (props.memoryTypes[i].propertyFlags & desired) == desired)
					return i;
			}
			return UINT32_MAX;
		}

		/// Crée + alloue + map un staging buffer HOST_VISIBLE | HOST_COHERENT
		/// puis copie `srcBytes` dedans. Le caller détruit `outBuf`/`outMem`
		/// après utilisation.
		bool CreateAndFillStagingBuffer(VkDevice device, VkPhysicalDevice physDev,
			const void* srcBytes, VkDeviceSize sizeBytes,
			VkBuffer& outBuf, VkDeviceMemory& outMem)
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = sizeBytes;
			bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateBuffer (staging) failed");
				return false;
			}

			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, outBuf, &req);
			const uint32_t memType = FindMemoryType(physDev, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (memType == UINT32_MAX)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] No HOST_VISIBLE memory");
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;
			if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkAllocateMemory (staging) failed");
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}
			if (vkBindBufferMemory(device, outBuf, outMem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkBindBufferMemory (staging) failed");
				vkFreeMemory(device, outMem, nullptr);
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				outMem = VK_NULL_HANDLE;
				return false;
			}

			void* mapped = nullptr;
			if (vkMapMemory(device, outMem, 0, sizeBytes, 0, &mapped) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkMapMemory (staging) failed");
				vkFreeMemory(device, outMem, nullptr);
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				outMem = VK_NULL_HANDLE;
				return false;
			}
			std::memcpy(mapped, srcBytes, static_cast<size_t>(sizeBytes));
			vkUnmapMemory(device, outMem);
			return true;
		}

		/// Crée un VkImage 2D OPTIMAL DEVICE_LOCAL (single layer ou array).
		bool CreateOptimalImage(VkDevice device, VkPhysicalDevice physDev,
			uint32_t width, uint32_t height, uint32_t layerCount,
			VkFormat format, VkImageUsageFlags usage,
			VkImage& outImage, VkDeviceMemory& outMem)
		{
			VkImageCreateInfo ici{};
			ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType     = VK_IMAGE_TYPE_2D;
			ici.format        = format;
			ici.extent        = { width, height, 1 };
			ici.mipLevels     = 1;
			ici.arrayLayers   = layerCount;
			ici.samples       = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
			ici.usage         = usage;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateImage failed ({}x{} layers={})",
					width, height, layerCount);
				return false;
			}

			VkMemoryRequirements req{};
			vkGetImageMemoryRequirements(device, outImage, &req);
			const uint32_t memType = FindMemoryType(physDev, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memType == UINT32_MAX)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] No DEVICE_LOCAL memory");
				vkDestroyImage(device, outImage, nullptr);
				outImage = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;
			if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkAllocateMemory (image) failed");
				vkDestroyImage(device, outImage, nullptr);
				outImage = VK_NULL_HANDLE;
				return false;
			}
			if (vkBindImageMemory(device, outImage, outMem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkBindImageMemory failed");
				vkFreeMemory(device, outMem, nullptr);
				vkDestroyImage(device, outImage, nullptr);
				outImage = VK_NULL_HANDLE;
				outMem = VK_NULL_HANDLE;
				return false;
			}
			return true;
		}

		/// Submit synchrone d'un command buffer one-shot : barrier UNDEFINED→DST,
		/// copyBufferToImage (multi-layer si needed), barrier DST→SHADER_READ_ONLY.
		bool SubmitImageUploadSync(VkDevice device, VkQueue queue, uint32_t queueFamily,
			VkBuffer staging, VkImage dstImage, uint32_t layerCount,
			const VkBufferImageCopy* regions)
		{
			VkCommandPool pool = VK_NULL_HANDLE;
			VkCommandPoolCreateInfo poolCI{};
			poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolCI.queueFamilyIndex = queueFamily;
			poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS)
				return false;

			VkCommandBuffer cmd = VK_NULL_HANDLE;
			VkCommandBufferAllocateInfo aci{};
			aci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			aci.commandPool        = pool;
			aci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			aci.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(device, &aci, &cmd) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &bi);

			// Barrier UNDEFINED → TRANSFER_DST_OPTIMAL.
			{
				VkImageMemoryBarrier b{};
				b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
				b.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image            = dstImage;
				b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount };
				b.srcAccessMask    = 0;
				b.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &b);
			}

			vkCmdCopyBufferToImage(cmd, staging, dstImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount, regions);

			// Barrier TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
			{
				VkImageMemoryBarrier b{};
				b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				b.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image            = dstImage;
				b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount };
				b.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
				b.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &b);
			}
			vkEndCommandBuffer(cmd);

			VkFence fence = VK_NULL_HANDLE;
			VkFenceCreateInfo fci{};
			fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			vkCreateFence(device, &fci, nullptr, &fence);

			VkSubmitInfo si{};
			si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			si.commandBufferCount = 1;
			si.pCommandBuffers    = &cmd;
			vkQueueSubmit(queue, 1, &si, fence);
			vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
			vkDestroyFence(device, fence, nullptr);
			vkDestroyCommandPool(device, pool, nullptr);
			return true;
		}

		/// Submit synchrone : barrier UNDEFINED→DST, copyBuffer (buffer→buffer),
		/// barrier DST→VERTEX_BUFFER_READ. Pour les VkBuffer DEVICE_LOCAL.
		bool SubmitBufferUploadSync(VkDevice device, VkQueue queue, uint32_t queueFamily,
			VkBuffer staging, VkBuffer dst, VkDeviceSize sizeBytes,
			VkAccessFlags dstAccess, VkPipelineStageFlags dstStage)
		{
			VkCommandPool pool = VK_NULL_HANDLE;
			VkCommandPoolCreateInfo poolCI{};
			poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolCI.queueFamilyIndex = queueFamily;
			poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS)
				return false;

			VkCommandBuffer cmd = VK_NULL_HANDLE;
			VkCommandBufferAllocateInfo aci{};
			aci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			aci.commandPool        = pool;
			aci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			aci.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(device, &aci, &cmd) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &bi);

			VkBufferCopy region{};
			region.size = sizeBytes;
			vkCmdCopyBuffer(cmd, staging, dst, 1, &region);

			// Barrier TRANSFER_WRITE → dstAccess (vertex/index read).
			VkBufferMemoryBarrier mb{};
			mb.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			mb.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
			mb.dstAccessMask       = dstAccess;
			mb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mb.buffer              = dst;
			mb.offset              = 0;
			mb.size                = sizeBytes;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage,
				0, 0, nullptr, 1, &mb, 0, nullptr);

			vkEndCommandBuffer(cmd);

			VkFence fence = VK_NULL_HANDLE;
			VkFenceCreateInfo fci{};
			fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			vkCreateFence(device, &fci, nullptr, &fence);

			VkSubmitInfo si{};
			si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			si.commandBufferCount = 1;
			si.pCommandBuffers    = &cmd;
			vkQueueSubmit(queue, 1, &si, fence);
			vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
			vkDestroyFence(device, fence, nullptr);
			vkDestroyCommandPool(device, pool, nullptr);
			return true;
		}
	} // namespace

	// ─────────────────────────────────────────────────────────────────────────
	// VulkanBufferAllocator
	// ─────────────────────────────────────────────────────────────────────────

	void VulkanBufferAllocator::Init(VkDevice device, VkPhysicalDevice physDev,
		VkQueue queue, uint32_t queueFamily)
	{
		m_device = device;
		m_physDev = physDev;
		m_queue = queue;
		m_queueFamily = queueFamily;
	}

	VkBuffer VulkanBufferAllocator::CreateAndUploadBuffer(const void* srcBytes,
		size_t sizeBytes, VkBufferUsageFlags usage)
	{
		if (m_device == VK_NULL_HANDLE || sizeBytes == 0u || srcBytes == nullptr)
			return VK_NULL_HANDLE;

		// Staging.
		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		if (!CreateAndFillStagingBuffer(m_device, m_physDev, srcBytes, sizeBytes, staging, stagingMem))
			return VK_NULL_HANDLE;

		// Destination DEVICE_LOCAL buffer.
		VkBuffer dst = VK_NULL_HANDLE;
		VkDeviceMemory dstMem = VK_NULL_HANDLE;
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = sizeBytes;
			bi.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(m_device, &bi, nullptr, &dst) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateBuffer (dst) failed");
				vkDestroyBuffer(m_device, staging, nullptr);
				vkFreeMemory(m_device, stagingMem, nullptr);
				return VK_NULL_HANDLE;
			}
			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(m_device, dst, &req);
			const uint32_t memType = FindMemoryType(m_physDev, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memType == UINT32_MAX)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] No DEVICE_LOCAL memory (vbo/ibo)");
				vkDestroyBuffer(m_device, dst, nullptr);
				vkDestroyBuffer(m_device, staging, nullptr);
				vkFreeMemory(m_device, stagingMem, nullptr);
				return VK_NULL_HANDLE;
			}
			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;
			if (vkAllocateMemory(m_device, &ai, nullptr, &dstMem) != VK_SUCCESS
				|| vkBindBufferMemory(m_device, dst, dstMem, 0) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[TerrainChunkRenderer] alloc/bind (vbo/ibo) failed");
				if (dstMem != VK_NULL_HANDLE) vkFreeMemory(m_device, dstMem, nullptr);
				vkDestroyBuffer(m_device, dst, nullptr);
				vkDestroyBuffer(m_device, staging, nullptr);
				vkFreeMemory(m_device, stagingMem, nullptr);
				return VK_NULL_HANDLE;
			}
		}

		// Submit copy + barrier.
		const VkAccessFlags dstAccess = (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
			? VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT : VK_ACCESS_INDEX_READ_BIT;
		const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		const bool ok = SubmitBufferUploadSync(m_device, m_queue, m_queueFamily,
			staging, dst, sizeBytes, dstAccess, dstStage);

		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMem, nullptr);

		if (!ok)
		{
			vkDestroyBuffer(m_device, dst, nullptr);
			vkFreeMemory(m_device, dstMem, nullptr);
			return VK_NULL_HANDLE;
		}

		m_owned[dst] = { dstMem };
		return dst;
	}

	VkBuffer VulkanBufferAllocator::CreateAndUploadVertexBuffer(const void* src, size_t bytes)
	{
		return CreateAndUploadBuffer(src, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	}

	VkBuffer VulkanBufferAllocator::CreateAndUploadIndexBuffer(const void* src, size_t bytes)
	{
		return CreateAndUploadBuffer(src, bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	}

	void VulkanBufferAllocator::DestroyBuffer(VkBuffer buffer)
	{
		if (buffer == VK_NULL_HANDLE || m_device == VK_NULL_HANDLE) return;
		auto it = m_owned.find(buffer);
		if (it == m_owned.end()) return;
		vkDestroyBuffer(m_device, buffer, nullptr);
		if (it->second.memory != VK_NULL_HANDLE)
			vkFreeMemory(m_device, it->second.memory, nullptr);
		m_owned.erase(it);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// VulkanImageAllocator (single-layer 2D)
	// ─────────────────────────────────────────────────────────────────────────

	void VulkanImageAllocator::Init(VkDevice device, VkPhysicalDevice physDev,
		VkQueue queue, uint32_t queueFamily)
	{
		m_device = device;
		m_physDev = physDev;
		m_queue = queue;
		m_queueFamily = queueFamily;
	}

	void VulkanImageAllocator::CreateAndUploadRGBA8Image(uint32_t width, uint32_t height,
		const void* srcBytes, VkImage& outImage, VkImageView& outView)
	{
		outImage = VK_NULL_HANDLE;
		outView = VK_NULL_HANDLE;
		if (m_device == VK_NULL_HANDLE || width == 0u || height == 0u || srcBytes == nullptr)
			return;

		const VkDeviceSize sizeBytes = static_cast<VkDeviceSize>(width) * height * 4u;

		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		if (!CreateAndFillStagingBuffer(m_device, m_physDev, srcBytes, sizeBytes, staging, stagingMem))
			return;

		VkDeviceMemory imgMem = VK_NULL_HANDLE;
		if (!CreateOptimalImage(m_device, m_physDev, width, height, 1u,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			outImage, imgMem))
		{
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			return;
		}

		VkBufferImageCopy region{};
		region.bufferOffset      = 0;
		region.bufferRowLength   = width;
		region.bufferImageHeight = height;
		region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageOffset       = { 0, 0, 0 };
		region.imageExtent       = { width, height, 1 };

		const bool ok = SubmitImageUploadSync(m_device, m_queue, m_queueFamily,
			staging, outImage, 1u, &region);

		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMem, nullptr);

		if (!ok)
		{
			vkDestroyImage(m_device, outImage, nullptr);
			vkFreeMemory(m_device, imgMem, nullptr);
			outImage = VK_NULL_HANDLE;
			return;
		}

		// Crée la view (2D, single layer).
		VkImageViewCreateInfo vci{};
		vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image                           = outImage;
		vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		vci.format                          = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.baseMipLevel   = 0;
		vci.subresourceRange.levelCount     = 1;
		vci.subresourceRange.baseArrayLayer = 0;
		vci.subresourceRange.layerCount     = 1;
		if (vkCreateImageView(m_device, &vci, nullptr, &outView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateImageView (splat) failed");
			vkDestroyImage(m_device, outImage, nullptr);
			vkFreeMemory(m_device, imgMem, nullptr);
			outImage = VK_NULL_HANDLE;
			return;
		}

		m_owned[outImage] = { imgMem };
	}

	void VulkanImageAllocator::DestroyImage(VkImage image, VkImageView view)
	{
		if (m_device == VK_NULL_HANDLE) return;
		if (view != VK_NULL_HANDLE) vkDestroyImageView(m_device, view, nullptr);
		if (image == VK_NULL_HANDLE) return;
		auto it = m_owned.find(image);
		vkDestroyImage(m_device, image, nullptr);
		if (it != m_owned.end())
		{
			if (it->second.memory != VK_NULL_HANDLE)
				vkFreeMemory(m_device, it->second.memory, nullptr);
			m_owned.erase(it);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// VulkanImageArrayAllocator (multi-layer 2D Array + samplers)
	// ─────────────────────────────────────────────────────────────────────────

	void VulkanImageArrayAllocator::Init(VkDevice device, VkPhysicalDevice physDev,
		VkQueue queue, uint32_t queueFamily)
	{
		m_device = device;
		m_physDev = physDev;
		m_queue = queue;
		m_queueFamily = queueFamily;
	}

	void VulkanImageArrayAllocator::CreateAndUploadRGBA8Array(uint32_t width, uint32_t height,
		uint32_t layerCount, const void* pixelData, VkImage& outImage, VkImageView& outView)
	{
		outImage = VK_NULL_HANDLE;
		outView = VK_NULL_HANDLE;
		if (m_device == VK_NULL_HANDLE || width == 0u || height == 0u
			|| layerCount == 0u || pixelData == nullptr)
			return;

		const VkDeviceSize layerBytes = static_cast<VkDeviceSize>(width) * height * 4u;
		const VkDeviceSize totalBytes = layerBytes * layerCount;

		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		if (!CreateAndFillStagingBuffer(m_device, m_physDev, pixelData, totalBytes, staging, stagingMem))
			return;

		VkDeviceMemory imgMem = VK_NULL_HANDLE;
		if (!CreateOptimalImage(m_device, m_physDev, width, height, layerCount,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			outImage, imgMem))
		{
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			return;
		}

		std::vector<VkBufferImageCopy> regions(layerCount);
		for (uint32_t l = 0; l < layerCount; ++l)
		{
			regions[l].bufferOffset      = layerBytes * l;
			regions[l].bufferRowLength   = width;
			regions[l].bufferImageHeight = height;
			regions[l].imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1 };
			regions[l].imageOffset       = { 0, 0, 0 };
			regions[l].imageExtent       = { width, height, 1 };
		}

		const bool ok = SubmitImageUploadSync(m_device, m_queue, m_queueFamily,
			staging, outImage, layerCount, regions.data());

		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMem, nullptr);

		if (!ok)
		{
			vkDestroyImage(m_device, outImage, nullptr);
			vkFreeMemory(m_device, imgMem, nullptr);
			outImage = VK_NULL_HANDLE;
			return;
		}

		VkImageViewCreateInfo vci{};
		vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image                           = outImage;
		vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		vci.format                          = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.baseMipLevel   = 0;
		vci.subresourceRange.levelCount     = 1;
		vci.subresourceRange.baseArrayLayer = 0;
		vci.subresourceRange.layerCount     = layerCount;
		if (vkCreateImageView(m_device, &vci, nullptr, &outView) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateImageView (array) failed");
			vkDestroyImage(m_device, outImage, nullptr);
			vkFreeMemory(m_device, imgMem, nullptr);
			outImage = VK_NULL_HANDLE;
			return;
		}

		m_owned[outImage] = { imgMem };
	}

	VkSampler VulkanImageArrayAllocator::CreateSampler(bool linear)
	{
		if (m_device == VK_NULL_HANDLE) return VK_NULL_HANDLE;
		VkSamplerCreateInfo sci{};
		sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter    = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		sci.minFilter    = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		// REPEAT pour le tiling des arrays PBR ; pour les splat-maps (nearest)
		// le shader sample en UV [0,1] donc REPEAT est sans effet à l'intérieur
		// du chunk — pas de différence pratique vs CLAMP_TO_EDGE.
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.maxLod       = 0.0f;
		VkSampler sampler = VK_NULL_HANDLE;
		if (vkCreateSampler(m_device, &sci, nullptr, &sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[TerrainChunkRenderer] vkCreateSampler failed (linear={})", linear);
			return VK_NULL_HANDLE;
		}
		return sampler;
	}

	void VulkanImageArrayAllocator::DestroyImage(VkImage image, VkImageView view)
	{
		if (m_device == VK_NULL_HANDLE) return;
		if (view != VK_NULL_HANDLE) vkDestroyImageView(m_device, view, nullptr);
		if (image == VK_NULL_HANDLE) return;
		auto it = m_owned.find(image);
		vkDestroyImage(m_device, image, nullptr);
		if (it != m_owned.end())
		{
			if (it->second.memory != VK_NULL_HANDLE)
				vkFreeMemory(m_device, it->second.memory, nullptr);
			m_owned.erase(it);
		}
	}

	void VulkanImageArrayAllocator::DestroySampler(VkSampler sampler)
	{
		if (sampler != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
			vkDestroySampler(m_device, sampler, nullptr);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// TerrainChunkRenderer
	// ─────────────────────────────────────────────────────────────────────────

	bool TerrainChunkRenderer::Init(VkDevice device, VkPhysicalDevice physDev,
		VkRenderPass renderPass, VkDescriptorSetLayout cameraSetLayout,
		VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex,
		engine::render::StagingAllocator* staging,
		engine::render::AssetRegistry* assetRegistry,
		engine::world::StreamCache* streamCache,
		const engine::core::Config& config,
		const std::string& contentRoot,
		const std::string& shaderRootPath,
		std::string& outError)
	{
		if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE
			|| renderPass == VK_NULL_HANDLE || cameraSetLayout == VK_NULL_HANDLE
			|| graphicsQueue == VK_NULL_HANDLE)
		{
			outError = "TerrainChunkRenderer::Init: invalid Vulkan args";
			return false;
		}

		m_device = device;
		m_physDev = physDev;
		m_graphicsQueue = graphicsQueue;
		m_graphicsQueueFamily = graphicsQueueFamilyIndex;
		m_staging = staging;
		m_assetRegistry = assetRegistry;
		m_streamCache = streamCache;
		m_config = &config;

		// 1. Pipeline graphique terrain_chunk (Phase 3a).
		if (!m_pipeline.Init(device, physDev, renderPass, cameraSetLayout,
			shaderRootPath, outError))
		{
			LOG_ERROR(Render, "[TerrainChunkRenderer] Pipeline init failed: {}", outError);
			return false;
		}

		// 2. Charge la palette (canonical: assets/terrain/layer_palette.json).
		engine::world::terrain::LayerPalette palette;
		const std::filesystem::path palettePath = "assets/terrain/layer_palette.json";
		std::string palErr;
		if (!engine::world::terrain::LoadLayerPalette(palettePath, palette, palErr))
		{
			outError = "Palette load failed: " + palErr;
			LOG_ERROR(Render, "[TerrainChunkRenderer] {}", outError);
			Shutdown(device);
			return false;
		}

		// 3. DescriptorPool sized pour 49 chunks (7x7 Visible ring).
		if (!m_descPool.Init(device, m_pipeline.GetSplatSetLayout(), 49u, outError))
		{
			LOG_ERROR(Render, "[TerrainChunkRenderer] DescPool init failed: {}", outError);
			Shutdown(device);
			return false;
		}

		// 4. Allocateurs Vulkan concrets (uploads synchrones boot + premier touch).
		m_bufferAlloc.Init(device, physDev, graphicsQueue, graphicsQueueFamilyIndex);
		m_imageAlloc.Init(device, physDev, graphicsQueue, graphicsQueueFamilyIndex);
		m_imageArrayAlloc.Init(device, physDev, graphicsQueue, graphicsQueueFamilyIndex);

		// 5. Caches GPU (mesh + splat).
		m_meshCache.Init(&m_bufferAlloc, &m_runtime);
		m_splatCache.Init(&m_imageAlloc, &m_runtime);

		// 6. LayerArrayLoader skeleton (alloc + samplers ; charge plus bas).
		std::string lalErr;
		if (!m_layerLoader.Init(&m_imageArrayAlloc, palette, contentRoot, lalErr))
		{
			outError = "LayerArrayLoader skeleton init failed: " + lalErr;
			LOG_ERROR(Render, "[TerrainChunkRenderer] {}", outError);
			Shutdown(device);
			return false;
		}

		// 7. Charge les 24 textures PBR + crée les 2 samplers + LayerParams UBO.
		if (!LoadAndUploadLayerArrays(palette, contentRoot, outError))
		{
			Shutdown(device);
			return false;
		}
		if (!CreateLayerParamsUbo(device, physDev, palette, outError))
		{
			Shutdown(device);
			return false;
		}

		// 8. Runtime LRU + budget.
		ChunkRuntime::Config runtimeCfg{};
		const int64_t budgetMb = config.GetInt("editor.world.terrain.gpu_budget_mb", 256);
		runtimeCfg.gpuBudgetBytes = static_cast<size_t>(
			(budgetMb > 0 ? budgetMb : 256)) * 1024ull * 1024ull;
		m_runtime.Init(runtimeCfg);

		LOG_INFO(Render,
			"[TerrainChunkRenderer] Init OK (budget={}MB, palette layers=8)",
			runtimeCfg.gpuBudgetBytes / (1024ull * 1024ull));
		return true;
	}

	bool TerrainChunkRenderer::LoadAndUploadLayerArrays(
		const engine::world::terrain::LayerPalette& palette,
		const std::string& contentRoot, std::string& outError)
	{
		// Pour chaque mapType, on charge les 8 PNG/texr via stb_image, on
		// concatène en un blob unique de `width*height*4*8` octets, puis on
		// uploade via l'allocator.

		struct LoadedLayer
		{
			std::vector<uint8_t> rgba; ///< width*height*4 octets
			int width = 0;
			int height = 0;
		};

		auto loadOneLayer = [&](uint32_t layerIdx, LayerMapType mapType) -> LoadedLayer {
			LoadedLayer ll;
			const std::filesystem::path p = ResolveLayerAssetPath(palette, layerIdx, mapType,
				contentRoot, [](const std::filesystem::path& q){ return std::filesystem::exists(q); });
			int w = 0, h = 0, c = 0;
			unsigned char* pixels = stbi_load(p.string().c_str(), &w, &h, &c, 4);
			if (pixels != nullptr && w > 0 && h > 0)
			{
				const size_t bytes = static_cast<size_t>(w) * h * 4u;
				ll.rgba.assign(pixels, pixels + bytes);
				ll.width = w;
				ll.height = h;
				stbi_image_free(pixels);
				return ll;
			}
			if (pixels != nullptr) stbi_image_free(pixels);
			LOG_WARN(Render,
				"[TerrainChunkRenderer] Couldn't load PBR layer {} mapType={} ('{}') — fallback magenta 1x1",
				layerIdx, static_cast<int>(mapType), p.string());
			ll.rgba = { 255u, 0u, 255u, 255u };
			ll.width = 1;
			ll.height = 1;
			return ll;
		};

		auto buildArrayBlob = [&](LayerMapType mapType, std::vector<uint8_t>& outBlob,
			uint32_t& outWidth, uint32_t& outHeight) -> bool
		{
			std::array<LoadedLayer, 8> layers{};
			for (uint32_t i = 0; i < 8u; ++i) layers[i] = loadOneLayer(i, mapType);

			// On suppose taille uniforme = celle du layer 0. Les autres layers
			// dont la taille diffère sont remplacés par magenta (taille du layer 0).
			const int W = layers[0].width;
			const int H = layers[0].height;
			if (W <= 0 || H <= 0)
			{
				outError = "Layer 0 has invalid dimensions";
				return false;
			}
			outWidth  = static_cast<uint32_t>(W);
			outHeight = static_cast<uint32_t>(H);
			const size_t layerBytes = static_cast<size_t>(W) * H * 4u;
			outBlob.assign(static_cast<size_t>(8) * layerBytes, 0u);
			for (uint32_t i = 0; i < 8u; ++i)
			{
				if (layers[i].width == W && layers[i].height == H
					&& layers[i].rgba.size() == layerBytes)
				{
					std::memcpy(outBlob.data() + i * layerBytes,
						layers[i].rgba.data(), layerBytes);
				}
				else
				{
					LOG_WARN(Render,
						"[TerrainChunkRenderer] Layer {} mapType={} dims={}x{} != ref {}x{} — fallback magenta",
						i, static_cast<int>(mapType), layers[i].width, layers[i].height, W, H);
					// Magenta plein.
					for (size_t px = 0; px < static_cast<size_t>(W) * H; ++px)
					{
						uint8_t* dst = outBlob.data() + i * layerBytes + px * 4u;
						dst[0] = 255u; dst[1] = 0u; dst[2] = 255u; dst[3] = 255u;
					}
				}
			}
			return true;
		};

		// Albedo.
		{
			std::vector<uint8_t> blob;
			uint32_t W = 0, H = 0;
			if (!buildArrayBlob(LayerMapType::Albedo, blob, W, H)) return false;
			VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
			m_imageArrayAlloc.CreateAndUploadRGBA8Array(W, H, 8u, blob.data(), img, view);
			if (img == VK_NULL_HANDLE)
			{
				outError = "Albedo array upload failed";
				return false;
			}
			LayerArrayResources& res = m_layerLoader.GetResourcesMutable();
			res.albedoArrayImage = img;
			res.albedoArrayView  = view;
		}

		// Normal.
		{
			std::vector<uint8_t> blob;
			uint32_t W = 0, H = 0;
			if (!buildArrayBlob(LayerMapType::Normal, blob, W, H)) return false;
			VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
			m_imageArrayAlloc.CreateAndUploadRGBA8Array(W, H, 8u, blob.data(), img, view);
			if (img == VK_NULL_HANDLE)
			{
				outError = "Normal array upload failed";
				return false;
			}
			LayerArrayResources& res = m_layerLoader.GetResourcesMutable();
			res.normalArrayImage = img;
			res.normalArrayView  = view;
		}

		// ARM (Ambient/Roughness/Metallic).
		{
			std::vector<uint8_t> blob;
			uint32_t W = 0, H = 0;
			if (!buildArrayBlob(LayerMapType::Arm, blob, W, H)) return false;
			VkImage img = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
			m_imageArrayAlloc.CreateAndUploadRGBA8Array(W, H, 8u, blob.data(), img, view);
			if (img == VK_NULL_HANDLE)
			{
				outError = "ARM array upload failed";
				return false;
			}
			LayerArrayResources& res = m_layerLoader.GetResourcesMutable();
			res.armArrayImage = img;
			res.armArrayView  = view;
		}

		// Samplers (1 nearest pour les splat-maps, 1 linear pour les arrays PBR).
		{
			LayerArrayResources& res = m_layerLoader.GetResourcesMutable();
			res.nearestSampler = m_imageArrayAlloc.CreateSampler(false);
			res.linearSampler  = m_imageArrayAlloc.CreateSampler(true);
			if (res.nearestSampler == VK_NULL_HANDLE || res.linearSampler == VK_NULL_HANDLE)
			{
				outError = "Sampler creation failed";
				return false;
			}
		}

		LOG_INFO(Render, "[TerrainChunkRenderer] PBR arrays uploaded (8 layers x 3 maps + 2 samplers)");
		return true;
	}

	bool TerrainChunkRenderer::CreateLayerParamsUbo(VkDevice device, VkPhysicalDevice physDev,
		const engine::world::terrain::LayerPalette& palette, std::string& outError)
	{
		// std140 layout pour `LayerParams` du shader :
		//   layout(set=2, binding=5) uniform LayerParams {
		//       vec4 tilingScale[8]; // padded vec4, valeur dans .x
		//   };
		// Taille = 8 * 16 = 128 octets.
		constexpr VkDeviceSize kSizeBytes = 128;
		m_layerParamsSize = kSizeBytes;

		VkBufferCreateInfo bi{};
		bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bi.size        = kSizeBytes;
		bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bi, nullptr, &m_layerParamsUbo) != VK_SUCCESS)
		{
			outError = "LayerParams UBO vkCreateBuffer failed";
			return false;
		}
		VkMemoryRequirements req{};
		vkGetBufferMemoryRequirements(device, m_layerParamsUbo, &req);
		const uint32_t memType = FindMemoryType(physDev, req.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memType == UINT32_MAX)
		{
			outError = "LayerParams UBO no HOST_VISIBLE memory";
			return false;
		}
		VkMemoryAllocateInfo ai{};
		ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize  = req.size;
		ai.memoryTypeIndex = memType;
		if (vkAllocateMemory(device, &ai, nullptr, &m_layerParamsMem) != VK_SUCCESS
			|| vkBindBufferMemory(device, m_layerParamsUbo, m_layerParamsMem, 0) != VK_SUCCESS)
		{
			outError = "LayerParams UBO alloc/bind failed";
			return false;
		}

		// Remplit .x = tilingMeters de chaque layer ; les 3 autres composantes
		// sont à 0 (padding std140).
		void* mapped = nullptr;
		if (vkMapMemory(device, m_layerParamsMem, 0, kSizeBytes, 0, &mapped) != VK_SUCCESS)
		{
			outError = "LayerParams UBO vkMapMemory failed";
			return false;
		}
		float buffer[32]{};
		for (uint32_t i = 0; i < 8u; ++i)
		{
			const float t = palette.layers[i].tilingMeters;
			buffer[i * 4 + 0] = (t > 0.0f) ? t : 4.0f;
			buffer[i * 4 + 1] = 0.0f;
			buffer[i * 4 + 2] = 0.0f;
			buffer[i * 4 + 3] = 0.0f;
		}
		std::memcpy(mapped, buffer, sizeof(buffer));
		vkUnmapMemory(device, m_layerParamsMem);
		return true;
	}

	void TerrainChunkRenderer::Shutdown(VkDevice device)
	{
		if (device == VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
			device = m_device;
		if (device == VK_NULL_HANDLE) return;

		// Ordre inverse de Init.
		m_descPool.Shutdown(device);
		m_meshCache.Shutdown();
		m_splatCache.Shutdown();
		m_layerLoader.Shutdown();

		if (m_layerParamsUbo != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_layerParamsUbo, nullptr);
			m_layerParamsUbo = VK_NULL_HANDLE;
		}
		if (m_layerParamsMem != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_layerParamsMem, nullptr);
			m_layerParamsMem = VK_NULL_HANDLE;
		}

		m_pipeline.Shutdown(device);

		m_slotToCoord.clear();
		m_device = VK_NULL_HANDLE;
		m_physDev = VK_NULL_HANDLE;
		m_graphicsQueue = VK_NULL_HANDLE;
		m_staging = nullptr;
		m_assetRegistry = nullptr;
		m_streamCache = nullptr;
		m_config = nullptr;
	}

	void TerrainChunkRenderer::RenderVisibleChunks(VkCommandBuffer cmd,
		VkDescriptorSet cameraSet,
		const engine::world::World& world,
		const std::vector<engine::world::GlobalChunkCoord>& visibleChunks)
	{
		if (!IsValid() || cmd == VK_NULL_HANDLE || cameraSet == VK_NULL_HANDLE
			|| m_streamCache == nullptr || m_config == nullptr)
			return;

		const LayerArrayResources& arrays = m_layerLoader.GetResources();
		if (arrays.albedoArrayView == VK_NULL_HANDLE
			|| arrays.normalArrayView == VK_NULL_HANDLE
			|| arrays.armArrayView == VK_NULL_HANDLE
			|| arrays.nearestSampler == VK_NULL_HANDLE
			|| arrays.linearSampler == VK_NULL_HANDLE)
		{
			return; // PBR pas chargé — n'essaie même pas de dessiner.
		}

		for (const auto& coord : visibleChunks)
		{
			// Lecture disque : skip si fichier absent.
			auto chunkPtr = m_streamCache->LoadTerrainChunk(*m_config, coord.x, coord.z);
			if (!chunkPtr) continue;
			auto splatPtr = m_streamCache->LoadSplatMap(*m_config, coord.x, coord.z);
			if (!splatPtr) continue;

			// Lazy upload mesh + splat (caches font la dedup + tracking budget).
			const auto mesh = m_meshCache.GetOrUpload(coord, *chunkPtr);
			if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
				continue;
			const auto splatGpu = m_splatCache.GetOrUpload(coord, *splatPtr);
			if (splatGpu.view0 == VK_NULL_HANDLE || splatGpu.view1 == VK_NULL_HANDLE)
				continue;

			// Tracking slot → coord pour le Tick.
			const auto slot = m_runtime.GetOrAllocateSlot(coord);
			m_slotToCoord[slot] = coord;
			m_runtime.UpdateRing(coord, world.GetRingForChunk(coord));
			m_runtime.Touch(slot);

			// Alloc descriptor set + écriture des 6 bindings.
			VkDescriptorSet descSet = m_descPool.Allocate(m_device);
			if (descSet == VK_NULL_HANDLE)
			{
				LOG_WARN(Render, "[TerrainChunkRenderer] Descriptor pool saturé ; chunk ({},{}) skippé",
					coord.x, coord.z);
				continue;
			}

			VkDescriptorImageInfo imgInfos[5]{};
			imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imgInfos[0].imageView   = splatGpu.view0;
			imgInfos[0].sampler     = arrays.nearestSampler;
			imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imgInfos[1].imageView   = splatGpu.view1;
			imgInfos[1].sampler     = arrays.nearestSampler;
			imgInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imgInfos[2].imageView   = arrays.albedoArrayView;
			imgInfos[2].sampler     = arrays.linearSampler;
			imgInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imgInfos[3].imageView   = arrays.normalArrayView;
			imgInfos[3].sampler     = arrays.linearSampler;
			imgInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imgInfos[4].imageView   = arrays.armArrayView;
			imgInfos[4].sampler     = arrays.linearSampler;

			VkDescriptorBufferInfo uboInfo{};
			uboInfo.buffer = m_layerParamsUbo;
			uboInfo.offset = 0;
			uboInfo.range  = m_layerParamsSize;

			VkWriteDescriptorSet writes[6]{};
			for (uint32_t i = 0; i < 5u; ++i)
			{
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = descSet;
				writes[i].dstBinding      = i;
				writes[i].descriptorCount = 1u;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgInfos[i];
			}
			writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[5].dstSet          = descSet;
			writes[5].dstBinding      = 5u;
			writes[5].descriptorCount = 1u;
			writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[5].pBufferInfo     = &uboInfo;
			vkUpdateDescriptorSets(m_device, 6u, writes, 0, nullptr);

			// Origine monde (mètres) du coin chunk-local (0,0,0).
			const float originX = static_cast<float>(engine::world::kChunkSize) * coord.x;
			const float originZ = static_cast<float>(engine::world::kChunkSize) * coord.z;
			m_pipeline.RecordChunkDraw(cmd, cameraSet, descSet, mesh, originX, 0.0f, originZ);
		}
	}

	void TerrainChunkRenderer::Tick(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) device = m_device;
		if (device == VK_NULL_HANDLE) return;

		// Eviction LRU des chunks Far excédant le budget.
		auto evictions = m_runtime.CollectEvictionsForBudget();
		for (auto slot : evictions)
		{
			engine::world::GlobalChunkCoord coord{0, 0};
			auto it = m_slotToCoord.find(slot);
			if (it != m_slotToCoord.end()) coord = it->second;
			else coord = m_runtime.GetCoordForSlot(slot);

			m_meshCache.Evict(coord);
			m_splatCache.Evict(coord);
			m_runtime.RemoveSlot(slot);
			m_slotToCoord.erase(slot);
		}

		// Reset descriptor pool : les sets de la frame précédente sont stateless.
		m_descPool.Reset(device);
	}
}
