#include "src/client/render/gi/DdgiUpdatePass.h"
#include "src/client/render/gi/DdgiVolume.h"
#include "src/client/render/PipelineCache.h"
#include "src/shared/core/Log.h"

#include <vulkan/vulkan.h>

namespace engine::render::gi
{
	namespace
	{
		/// Crée un VkShaderModule à partir de mots SPIR-V. Renvoie VK_NULL_HANDLE
		/// si les arguments sont invalides ou si la création Vulkan échoue.
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* spirv, size_t wordCount)
		{
			if (device == VK_NULL_HANDLE || !spirv || wordCount == 0)
				return VK_NULL_HANDLE;

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = wordCount * sizeof(uint32_t);
			createInfo.pCode = spirv;

			VkShaderModule shaderModule = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			return shaderModule;
		}
	} // namespace anonyme

	bool DdgiUpdatePass::Init(VkDevice device, VkPhysicalDevice /*phys*/,
		const uint32_t* compSpirv, size_t compWordCount,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !compSpirv || compWordCount == 0)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: parametres invalides");
			return false;
		}

		// --- Descriptor set layout : 0 = storage image (irradiance), 1 = sampled (shadow cascade 0) ---
		VkDescriptorSetLayoutBinding bindings[2]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 2;
		layoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: descriptor set layout");
			Destroy(device);
			return false;
		}

		// --- Descriptor pool : 1 set (storage + sampled) ---
		VkDescriptorPoolSize poolSizes[2]{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: descriptor pool");
			Destroy(device);
			return false;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayout;
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: descriptor set");
			Destroy(device);
			return false;
		}

		// --- Pipeline layout : push constants DdgiUpdateParams (compute) ---
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = static_cast<uint32_t>(sizeof(DdgiUpdateParams));

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushRange;
		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: pipeline layout");
			Destroy(device);
			return false;
		}

		// --- Pipeline compute ---
		VkShaderModule shaderModule = CreateShaderModule(device, compSpirv, compWordCount);
		if (shaderModule == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: shader module");
			Destroy(device);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = m_pipelineLayout;
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = shaderModule;
		pipelineInfo.stage.pName = "main";
		AssertPipelineCreationAllowed();
		PipelineCache::RegisterWarmupKey(HashComputePsoKey(m_pipelineLayout, compWordCount));
		if (vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: compute pipeline");
			Destroy(device);
			return false;
		}
		vkDestroyShaderModule(device, shaderModule, nullptr);

		// --- Sampler de lecture shadow (NEAREST clamp ; occlusion grossière) ---
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[DdgiUpdatePass] Init FAILED: sampler");
			Destroy(device);
			return false;
		}

		LOG_INFO(Render, "[DdgiUpdatePass] Init OK");
		return true;
	}

	void DdgiUpdatePass::Record(VkDevice device, VkCommandBuffer cmd, const DdgiVolume& volume,
		VkImageView shadowCascade0, VkSampler shadowSamp,
		const DdgiUpdateParams& params)
	{
		if (!IsValid() || device == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE)
			return;

		const VkImageView irradianceView = volume.IrradianceView();
		if (irradianceView == VK_NULL_HANDLE || !volume.IsAllocated())
		{
			LOG_WARN(Render, "[DdgiUpdatePass] Record skip: volume non alloue");
			return;
		}

		// La shadow et le sampler doivent être valides (l'occlusion soleil les lit).
		// L'appelant garantit normalement leur présence ; on borne par sécurité.
		const VkSampler usedSampler = (shadowSamp != VK_NULL_HANDLE) ? shadowSamp : m_shadowSampler;
		if (shadowCascade0 == VK_NULL_HANDLE || usedSampler == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[DdgiUpdatePass] Record skip: shadow cascade 0 indisponible");
			return;
		}

		const uint32_t atlasW = volume.IrradianceAtlasWidth();
		const uint32_t atlasH = volume.IrradianceAtlasHeight();
		if (atlasW == 0u || atlasH == 0u)
			return;

		// --- Barrière : atlas irradiance -> GENERAL (écriture storage) ---
		// On part de SHADER_READ_ONLY si déjà lu une frame précédente, sinon de
		// UNDEFINED (première frame). Comme on ne mémorise pas l'état (image hors
		// frame graph, persistante au volume), on suppose le cas courant
		// SHADER_READ_ONLY -> GENERAL ; UNDEFINED resterait correct mais on
		// conserve le contenu pour le blend temporel, donc on ne discarde pas.
		// Pour la toute première frame le contenu est indéfini : l'hysteresis
		// converge en quelques frames, ce qui est acceptable pour une base de GI.
		VkImageMemoryBarrier toGeneral{};
		toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toGeneral.image = volume.IrradianceImage();
		toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toGeneral.subresourceRange.baseMipLevel = 0;
		toGeneral.subresourceRange.levelCount = 1;
		toGeneral.subresourceRange.baseArrayLayer = 0;
		toGeneral.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toGeneral);

		// --- Mise à jour du descriptor set (vue storage du volume + shadow) ---
		VkDescriptorImageInfo irradianceInfo{};
		irradianceInfo.imageView = irradianceView;
		irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo shadowInfo{};
		shadowInfo.sampler = usedSampler;
		shadowInfo.imageView = shadowCascade0;
		shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet writes[2]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_descriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[0].descriptorCount = 1;
		writes[0].pImageInfo = &irradianceInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_descriptorSet;
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &shadowInfo;
		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

		// --- Dispatch (groupes 8x8 couvrant l'atlas) ---
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DdgiUpdateParams), &params);
		vkCmdDispatch(cmd, (atlasW + 7u) / 8u, (atlasH + 7u) / 8u, 1u);

		// --- Barrière : GENERAL -> SHADER_READ_ONLY (lecture par le LightingPass) ---
		VkImageMemoryBarrier toRead{};
		toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toRead.image = volume.IrradianceImage();
		toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toRead.subresourceRange.baseMipLevel = 0;
		toRead.subresourceRange.levelCount = 1;
		toRead.subresourceRange.baseArrayLayer = 0;
		toRead.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toRead);
	}

	void DdgiUpdatePass::Destroy(VkDevice device)
	{
		if (device != VK_NULL_HANDLE)
		{
			if (m_shadowSampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(device, m_shadowSampler, nullptr);
				m_shadowSampler = VK_NULL_HANDLE;
			}
			if (m_pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device, m_pipeline, nullptr);
				m_pipeline = VK_NULL_HANDLE;
			}
			if (m_pipelineLayout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
				m_pipelineLayout = VK_NULL_HANDLE;
			}
			if (m_descriptorPool != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
				m_descriptorPool = VK_NULL_HANDLE;
			}
			if (m_descriptorSetLayout != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
				m_descriptorSetLayout = VK_NULL_HANDLE;
			}
		}
		m_descriptorSet = VK_NULL_HANDLE;
	}
} // namespace engine::render::gi
