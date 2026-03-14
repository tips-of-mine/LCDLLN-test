#include "engine/render/Material.h"
#include "engine/core/Log.h"
#include "engine/render/vk/VkUtils.h"

#include <vulkan/vulkan_core.h>

#include <cstring>

namespace engine::render
{
	namespace
	{
		uint64_t MakeTextureKey(AssetId assetId, bool srgbSampler)
		{
			return (static_cast<uint64_t>(assetId) << 1u) | static_cast<uint64_t>(srgbSampler ? 1u : 0u);
		}
	}

	bool MaterialDescriptorCache::createFallbackTexture(VkDevice device,
	    VkPhysicalDevice physDev, uint32_t idx, const uint8_t rgba[4], VkFormat format)
	{
		VkImageCreateInfo imgInfo{};
		imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType     = VK_IMAGE_TYPE_2D;
		imgInfo.format        = format;
		imgInfo.extent        = { 1, 1, 1 };
		imgInfo.mipLevels     = 1;
		imgInfo.arrayLayers   = 1;
		imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling        = VK_IMAGE_TILING_LINEAR;
		imgInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imgInfo, nullptr, &m_fallbackImage[idx]) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Fallback image create FAILED (slot={})", idx);
			return false;
		}

		VkMemoryRequirements mr{};
		vkGetImageMemoryRequirements(device, m_fallbackImage[idx], &mr);

		const uint32_t memType = engine::render::vk::FindMemoryType(physDev, mr.memoryTypeBits,
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memType == UINT32_MAX)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Fallback image create FAILED: no memory type (slot={})", idx);
			vkDestroyImage(device, m_fallbackImage[idx], nullptr);
			m_fallbackImage[idx] = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = mr.size;
		allocInfo.memoryTypeIndex = memType;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_fallbackMemory[idx]) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Fallback image create FAILED: memory allocation (slot={})", idx);
			vkDestroyImage(device, m_fallbackImage[idx], nullptr);
			m_fallbackImage[idx] = VK_NULL_HANDLE;
			return false;
		}
		vkBindImageMemory(device, m_fallbackImage[idx], m_fallbackMemory[idx], 0);

		VkImageSubresource subres{};
		subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout layout{};
		vkGetImageSubresourceLayout(device, m_fallbackImage[idx], &subres, &layout);

		void* ptr = nullptr;
		vkMapMemory(device, m_fallbackMemory[idx], 0, mr.size, 0, &ptr);
		uint8_t* dst = static_cast<uint8_t*>(ptr) + layout.offset;
		dst[0] = rgba[0];
		dst[1] = rgba[1];
		dst[2] = rgba[2];
		dst[3] = rgba[3];
		vkUnmapMemory(device, m_fallbackMemory[idx]);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image                           = m_fallbackImage[idx];
		viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format                          = format;
		viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel   = 0;
		viewInfo.subresourceRange.levelCount     = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount     = 1;
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_fallbackView[idx]) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Fallback image create FAILED: image view (slot={})", idx);
			vkFreeMemory(device, m_fallbackMemory[idx], nullptr);
			vkDestroyImage(device, m_fallbackImage[idx], nullptr);
			m_fallbackMemory[idx] = VK_NULL_HANDLE;
			m_fallbackImage[idx]  = VK_NULL_HANDLE;
			return false;
		}
		LOG_INFO(Render, "[MaterialDescriptorCache] Fallback texture ready (slot={})", idx);
		return true;
	}

	bool MaterialDescriptorCache::createMaterialBuffer(VkDevice device, VkPhysicalDevice physicalDevice)
	{
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(m_maxMaterials) * sizeof(MaterialGpuData);
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_materialBuffer) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: material buffer creation failed");
			return false;
		}

		VkMemoryRequirements requirements{};
		vkGetBufferMemoryRequirements(device, m_materialBuffer, &requirements);
		const uint32_t memoryType = engine::render::vk::FindMemoryType(
			physicalDevice,
			requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memoryType == UINT32_MAX)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: material buffer memory type missing");
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = memoryType;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_materialMemory) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: material buffer memory allocation failed");
			return false;
		}

		if (vkBindBufferMemory(device, m_materialBuffer, m_materialMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: material buffer bind failed");
			return false;
		}
		LOG_INFO(Render, "[MaterialDescriptorCache] Material buffer created (max_materials={})", m_maxMaterials);
		return true;
	}

	bool MaterialDescriptorCache::writeTextureDescriptors(VkDevice device)
	{
		if (m_descriptorSet == VK_NULL_HANDLE || m_textureSlots.empty())
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Texture descriptor update FAILED: descriptor set or slots missing");
			return false;
		}

		std::vector<VkDescriptorImageInfo> imageInfos(m_maxTextures);
		for (uint32_t i = 0; i < m_maxTextures; ++i)
		{
			const TextureSlot& slot = m_textureSlots[i];
			imageInfos[i].sampler = slot.sampler != VK_NULL_HANDLE ? slot.sampler : m_samplerLinear;
			imageInfos[i].imageView = slot.view != VK_NULL_HANDLE ? slot.view : m_fallbackView[0];
			imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_materialBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writes[2]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_descriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = m_maxTextures;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = imageInfos.data();

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_descriptorSet;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
		return true;
	}

	bool MaterialDescriptorCache::writeMaterialData(VkDevice device, uint32_t materialIndex, const MaterialGpuData& gpuData)
	{
		if (device == VK_NULL_HANDLE || m_materialMemory == VK_NULL_HANDLE || materialIndex >= m_materials.size())
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Material write FAILED: invalid parameters");
			return false;
		}

		void* mapped = nullptr;
		if (vkMapMemory(device, m_materialMemory,
				static_cast<VkDeviceSize>(materialIndex) * sizeof(MaterialGpuData),
				sizeof(MaterialGpuData), 0, &mapped) != VK_SUCCESS || mapped == nullptr)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Material write FAILED: map failed (index={})", materialIndex);
			return false;
		}
		std::memcpy(mapped, &gpuData, sizeof(MaterialGpuData));
		vkUnmapMemory(device, m_materialMemory);
		return true;
	}

	uint32_t MaterialDescriptorCache::acquireTextureSlot(const TextureHandle& texture, VkSampler sampler, VkImageView fallbackView)
	{
		if (!texture.IsValid())
		{
			for (uint32_t i = 0; i < 3 && i < m_textureSlots.size(); ++i)
			{
				if (m_textureSlots[i].view == fallbackView)
					return i;
			}
			return 0u;
		}

		TextureAsset* asset = texture.Get();
		if (!asset || asset->view == VK_NULL_HANDLE)
			return 0u;

		const uint64_t key = MakeTextureKey(texture.Id(), sampler == m_samplerSrgb);
		auto it = m_textureKeyToSlot.find(key);
		if (it != m_textureKeyToSlot.end())
			return it->second;

		for (uint32_t slotIndex = 3; slotIndex < m_textureSlots.size(); ++slotIndex)
		{
			if (m_textureSlots[slotIndex].view == VK_NULL_HANDLE)
			{
				m_textureSlots[slotIndex].assetId = texture.Id();
				m_textureSlots[slotIndex].view = asset->view;
				m_textureSlots[slotIndex].sampler = sampler;
				m_textureKeyToSlot[key] = slotIndex;
				return slotIndex;
			}
		}

		LOG_WARN(Render, "[MaterialDescriptorCache] Texture slot pool exhausted (asset_id={})", texture.Id());
		return 0u;
	}

	bool MaterialDescriptorCache::Init(VkDevice device, VkPhysicalDevice physicalDevice,
	    uint32_t maxTextures, uint32_t maxMaterials)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || maxTextures < 3u || maxMaterials == 0)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: invalid parameters");
			return false;
		}

		m_maxTextures = maxTextures;
		m_maxMaterials = maxMaterials;
		m_textureSlots.assign(m_maxTextures, {});
		m_materials.clear();
		m_textureKeyToSlot.clear();

		{
			VkSamplerCreateInfo si{};
			si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			si.maxLod       = VK_LOD_CLAMP_NONE;
			if (vkCreateSampler(device, &si, nullptr, &m_samplerSrgb) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: sRGB sampler creation failed");
				return false;
			}
			if (vkCreateSampler(device, &si, nullptr, &m_samplerLinear) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: linear sampler creation failed");
				Destroy(device);
				return false;
			}
		}

		const uint8_t whiteRgba[4]      = { 255, 255, 255, 255 };
		const uint8_t flatNormalRgba[4] = { 128, 128, 255, 255 };
		const uint8_t defaultOrmRgba[4] = { 255, 128,   0, 255 };
		if (!createFallbackTexture(device, physicalDevice, 0, whiteRgba,      VK_FORMAT_R8G8B8A8_SRGB) ||
		    !createFallbackTexture(device, physicalDevice, 1, flatNormalRgba, VK_FORMAT_R8G8B8A8_UNORM) ||
		    !createFallbackTexture(device, physicalDevice, 2, defaultOrmRgba, VK_FORMAT_R8G8B8A8_UNORM))
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: fallback textures creation failed");
			Destroy(device);
			return false;
		}

		{
			VkDescriptorSetLayoutBinding bindings[2]{};
			bindings[0].binding = 0;
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].descriptorCount = m_maxTextures;
			bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[1].binding = 1;
			bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[1].descriptorCount = 1;
			bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 2;
			layoutInfo.pBindings = bindings;
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: descriptor set layout creation failed");
				Destroy(device);
				return false;
			}
		}

		{
			VkDescriptorPoolSize poolSizes[2]{};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = m_maxTextures;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSizes[1].descriptorCount = 1;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 1;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: descriptor pool creation failed");
				Destroy(device);
				return false;
			}
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_layout;
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: descriptor set allocation failed");
			Destroy(device);
			return false;
		}

		if (!createMaterialBuffer(device, physicalDevice))
		{
			Destroy(device);
			return false;
		}

		m_textureSlots[0] = { 0u, m_fallbackView[0], m_samplerSrgb };
		m_textureSlots[1] = { 1u, m_fallbackView[1], m_samplerLinear };
		m_textureSlots[2] = { 2u, m_fallbackView[2], m_samplerLinear };
		if (!writeTextureDescriptors(device))
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: descriptor write failed");
			Destroy(device);
			return false;
		}

		const MaterialGpuData defaultMaterial{};
		m_materials.push_back(defaultMaterial);
		if (!writeMaterialData(device, 0u, defaultMaterial))
		{
			LOG_ERROR(Render, "[MaterialDescriptorCache] Init FAILED: default material upload failed");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "[MaterialDescriptorCache] Init OK (max_textures={}, max_materials={})", m_maxTextures, m_maxMaterials);
		return true;
	}

	uint32_t MaterialDescriptorCache::CreateMaterial(VkDevice device, const Material& mat)
	{
		if (!IsValid())
		{
			LOG_WARN(Render, "[MaterialDescriptorCache] CreateMaterial skipped: cache not initialized");
			return 0u;
		}
		if (m_materials.size() >= m_maxMaterials)
		{
			LOG_WARN(Render, "[MaterialDescriptorCache] Material pool exhausted");
			return 0u;
		}

		MaterialGpuData gpuData{};
		gpuData.baseColorIndex = acquireTextureSlot(mat.baseColor, m_samplerSrgb, m_fallbackView[0]);
		gpuData.normalIndex = acquireTextureSlot(mat.normal, m_samplerLinear, m_fallbackView[1]);
		gpuData.ormIndex = acquireTextureSlot(mat.orm, m_samplerLinear, m_fallbackView[2]);
		gpuData.flags = static_cast<uint32_t>(mat.flags);
		gpuData.tiling[0] = mat.tiling[0];
		gpuData.tiling[1] = mat.tiling[1];

		const uint32_t materialIndex = static_cast<uint32_t>(m_materials.size());
		m_materials.push_back(gpuData);
		if (!writeTextureDescriptors(device) || !writeMaterialData(device, materialIndex, gpuData))
		{
			m_materials.pop_back();
			LOG_ERROR(Render, "[MaterialDescriptorCache] CreateMaterial FAILED (index={})", materialIndex);
			return 0u;
		}

		LOG_INFO(Render, "[MaterialDescriptorCache] Material registered (index={}, bc_slot={}, nrm_slot={}, orm_slot={})",
			materialIndex, gpuData.baseColorIndex, gpuData.normalIndex, gpuData.ormIndex);
		return materialIndex;
	}

	void MaterialDescriptorCache::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "[MaterialDescriptorCache] Destroyed");
			return;
		}

		if (m_materialBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_materialBuffer, nullptr);
			m_materialBuffer = VK_NULL_HANDLE;
		}
		if (m_materialMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_materialMemory, nullptr);
			m_materialMemory = VK_NULL_HANDLE;
		}
		if (m_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_pool, nullptr);
			m_pool = VK_NULL_HANDLE;
		}
		if (m_layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
			m_layout = VK_NULL_HANDLE;
		}
		m_descriptorSet = VK_NULL_HANDLE;

		for (uint32_t i = 0; i < 3; ++i)
		{
			if (m_fallbackView[i] != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device, m_fallbackView[i], nullptr);
				m_fallbackView[i] = VK_NULL_HANDLE;
			}
			if (m_fallbackImage[i] != VK_NULL_HANDLE)
			{
				vkDestroyImage(device, m_fallbackImage[i], nullptr);
				m_fallbackImage[i] = VK_NULL_HANDLE;
			}
			if (m_fallbackMemory[i] != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, m_fallbackMemory[i], nullptr);
				m_fallbackMemory[i] = VK_NULL_HANDLE;
			}
		}
		if (m_samplerLinear != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_samplerLinear, nullptr);
			m_samplerLinear = VK_NULL_HANDLE;
		}
		if (m_samplerSrgb != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_samplerSrgb, nullptr);
			m_samplerSrgb = VK_NULL_HANDLE;
		}

		m_textureSlots.clear();
		m_materials.clear();
		m_textureKeyToSlot.clear();
		m_maxTextures = 0;
		m_maxMaterials = 0;
		LOG_INFO(Render, "[MaterialDescriptorCache] Destroyed");
	}
} // namespace engine::render
