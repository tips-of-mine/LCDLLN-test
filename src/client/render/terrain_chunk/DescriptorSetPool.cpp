#include "src/client/render/terrain_chunk/DescriptorSetPool.h"

namespace engine::render::terrain_chunk
{
	bool DescriptorSetPool::Init(VkDevice device, VkDescriptorSetLayout splatSetLayout,
		uint32_t maxSets, std::string& outError)
	{
		if (device == VK_NULL_HANDLE || splatSetLayout == VK_NULL_HANDLE || maxSets == 0u)
		{
			outError = "DescriptorSetPool::Init: invalid arguments";
			return false;
		}

		m_layout = splatSetLayout;
		m_maxSets = maxSets;
		m_allocatedSets = 0;

		// 5 combined image samplers + 1 UBO par set, scaled au max.
		// Aligné sur le set 2 de terrain_chunk.frag (Phase 3a).
		VkDescriptorPoolSize sizes[2]{};
		sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[0].descriptorCount = 5u * maxSets;
		sizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sizes[1].descriptorCount = 1u * maxSets;

		VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.flags = 0;
		info.maxSets = maxSets;
		info.poolSizeCount = 2u;
		info.pPoolSizes = sizes;

		if (vkCreateDescriptorPool(device, &info, nullptr, &m_pool) != VK_SUCCESS)
		{
			outError = "DescriptorSetPool::Init: vkCreateDescriptorPool failed";
			return false;
		}
		return true;
	}

	void DescriptorSetPool::Shutdown(VkDevice device)
	{
		if (m_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_pool, nullptr);
			m_pool = VK_NULL_HANDLE;
		}
		m_layout = VK_NULL_HANDLE;
		m_maxSets = 0;
		m_allocatedSets = 0;
	}

	VkDescriptorSet DescriptorSetPool::Allocate(VkDevice device)
	{
		if (m_pool == VK_NULL_HANDLE || m_layout == VK_NULL_HANDLE) return VK_NULL_HANDLE;
		if (m_allocatedSets >= m_maxSets) return VK_NULL_HANDLE;

		VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		info.descriptorPool = m_pool;
		info.descriptorSetCount = 1u;
		info.pSetLayouts = &m_layout;

		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(device, &info, &set) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		++m_allocatedSets;
		return set;
	}

	void DescriptorSetPool::Reset(VkDevice device)
	{
		if (m_pool == VK_NULL_HANDLE) return;
		vkResetDescriptorPool(device, m_pool, 0);
		m_allocatedSets = 0;
	}
}
