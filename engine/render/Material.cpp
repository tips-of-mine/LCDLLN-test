#include "engine/render/Material.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint32_t MaterialDescriptorCache::findMemoryType(VkPhysicalDevice physDev,
	    uint32_t filter, VkMemoryPropertyFlags props) const
	{
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
				return i;
		}
		return UINT32_MAX;
	}

	bool MaterialDescriptorCache::createFallbackTexture(VkDevice device,
	    VkPhysicalDevice physDev, uint32_t idx, const uint8_t rgba[4], VkFormat format)
	{
		// Create a 1×1 LINEAR host-visible image so we can write pixel data directly
		// without a staging command buffer (same pattern as AssetRegistry).
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
			LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateImage fallback[{}] failed", idx);
			return false;
		}

		VkMemoryRequirements mr{};
		vkGetImageMemoryRequirements(device, m_fallbackImage[idx], &mr);

		uint32_t memType = findMemoryType(physDev, mr.memoryTypeBits,
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memType == UINT32_MAX)
		{
			LOG_ERROR(Render, "MaterialDescriptorCache: no suitable memory type for fallback[{}]", idx);
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
			LOG_ERROR(Render, "MaterialDescriptorCache: vkAllocateMemory fallback[{}] failed", idx);
			vkDestroyImage(device, m_fallbackImage[idx], nullptr);
			m_fallbackImage[idx] = VK_NULL_HANDLE;
			return false;
		}
		vkBindImageMemory(device, m_fallbackImage[idx], m_fallbackMemory[idx], 0);

		// Write the single RGBA pixel via host mapping.
		{
			VkImageSubresource  subres{};
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
		}

		// Create image view.
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
			LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateImageView fallback[{}] failed", idx);
			vkFreeMemory(device, m_fallbackMemory[idx], nullptr);
			vkDestroyImage(device, m_fallbackImage[idx], nullptr);
			m_fallbackMemory[idx] = VK_NULL_HANDLE;
			m_fallbackImage[idx]  = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// MaterialDescriptorCache::Init
	// -------------------------------------------------------------------------

	bool MaterialDescriptorCache::Init(VkDevice device, VkPhysicalDevice physicalDevice,
	    uint32_t maxMaterials)
	{
		// --- 1. Samplers ---

		// BaseColor sampler: linear filter, repeat wrap (sRGB-compatible).
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
				LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateSampler (sRGB) failed");
				return false;
			}
		}

		// Normal/ORM sampler: linear filter, repeat wrap.
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
			if (vkCreateSampler(device, &si, nullptr, &m_samplerLinear) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateSampler (linear) failed");
				Destroy(device);
				return false;
			}
		}

		// --- 2. Fallback 1×1 textures ---
		// [0] BaseColor: opaque white (R8G8B8A8_SRGB).
		const uint8_t whiteRgba[4]      = { 255, 255, 255, 255 };
		// [1] Normal: flat tangent-space normal (0.5,0.5,1.0 encoded as 128,128,255) — R8G8B8A8_UNORM.
		const uint8_t flatNormalRgba[4] = { 128, 128, 255, 255 };
		// [2] ORM: AO=1.0(255), Roughness=0.5(128), Metallic=0.0(0) — R8G8B8A8_UNORM.
		const uint8_t defaultOrmRgba[4] = { 255, 128,   0, 255 };

		if (!createFallbackTexture(device, physicalDevice, 0, whiteRgba,      VK_FORMAT_R8G8B8A8_SRGB)  ||
		    !createFallbackTexture(device, physicalDevice, 1, flatNormalRgba, VK_FORMAT_R8G8B8A8_UNORM) ||
		    !createFallbackTexture(device, physicalDevice, 2, defaultOrmRgba, VK_FORMAT_R8G8B8A8_UNORM))
		{
			LOG_ERROR(Render, "MaterialDescriptorCache: failed to create one or more fallback textures");
			Destroy(device);
			return false;
		}

		// --- 3. Descriptor set layout: 3 combined image samplers ---
		// set = 0, binding 0=BaseColor, 1=Normal, 2=ORM — fragment stage.
		{
			VkDescriptorSetLayoutBinding bindings[3]{};
			for (uint32_t i = 0; i < 3; ++i)
			{
				bindings[i].binding         = i;
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings[i].pImmutableSamplers = nullptr;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 3;
			layoutInfo.pBindings    = bindings;
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateDescriptorSetLayout failed");
				Destroy(device);
				return false;
			}
		}

		// --- 4. Descriptor pool ---
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 3 * maxMaterials; // 3 bindings per material

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = maxMaterials;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "MaterialDescriptorCache: vkCreateDescriptorPool failed");
				Destroy(device);
				return false;
			}
		}

		LOG_INFO(Render, "MaterialDescriptorCache: initialized (maxMaterials={})", maxMaterials);
		return true;
	}

	// -------------------------------------------------------------------------
	// MaterialDescriptorCache::CreateDescriptorSet
	// -------------------------------------------------------------------------

	VkDescriptorSet MaterialDescriptorCache::CreateDescriptorSet(VkDevice device, const Material& mat)
	{
		if (!IsValid()) return VK_NULL_HANDLE;

		// Allocate one descriptor set from the pool.
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool     = m_pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts        = &m_layout;

		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "MaterialDescriptorCache: vkAllocateDescriptorSets failed");
			return VK_NULL_HANDLE;
		}

		// Resolve image views: use loaded texture if valid, else fallback.
		VkImageView views[3]    = { m_fallbackView[0], m_fallbackView[1], m_fallbackView[2] };
		VkSampler   samplers[3] = { m_samplerSrgb, m_samplerLinear, m_samplerLinear };

		if (TextureAsset* bc = mat.baseColor.Get(); bc && bc->view != VK_NULL_HANDLE)
			views[0] = bc->view;
		if (TextureAsset* nrm = mat.normal.Get(); nrm && nrm->view != VK_NULL_HANDLE)
			views[1] = nrm->view;
		if (TextureAsset* orm = mat.orm.Get(); orm && orm->view != VK_NULL_HANDLE)
			views[2] = orm->view;

		// Write all 3 combined-image-sampler bindings.
		VkDescriptorImageInfo imageInfos[3]{};
		VkWriteDescriptorSet  writes[3]{};
		for (uint32_t i = 0; i < 3; ++i)
		{
			imageInfos[i].sampler     = samplers[i];
			imageInfos[i].imageView   = views[i];
			imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = set;
			writes[i].dstBinding      = i;
			writes[i].dstArrayElement = 0;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

		LOG_INFO(Render, "MaterialDescriptorCache: created descriptor set (bc={}, nrm={}, orm={})",
		    mat.baseColor.IsValid() ? "loaded" : "fallback",
		    mat.normal.IsValid()    ? "loaded" : "fallback",
		    mat.orm.IsValid()       ? "loaded" : "fallback");

		return set;
	}

	// -------------------------------------------------------------------------
	// MaterialDescriptorCache::Destroy
	// -------------------------------------------------------------------------

	void MaterialDescriptorCache::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

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
	}

} // namespace engine::render
