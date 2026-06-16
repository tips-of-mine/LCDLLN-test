#include "src/client/render/SkillIconCache.h"

#include "src/client/render/AssetRegistry.h"
#include "src/shared/core/Log.h"

#if defined(_WIN32)
#	include "imgui_impl_vulkan.h"
#endif

namespace engine::client
{
	bool SkillIconCache::Init(VkDevice device, engine::render::AssetRegistry* registry)
	{
		m_device = device;
		m_registry = registry;
		if (m_device == VK_NULL_HANDLE || m_registry == nullptr)
		{
			LOG_WARN(Render, "[SkillIconCache] Init ignore (device/registry nul) — icones desactivees");
			return false;
		}

		// Sampler lineaire (clamp) — calque sur RacePreviewViewport.
		VkSamplerCreateInfo si{};
		si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		si.magFilter = VK_FILTER_LINEAR;
		si.minFilter = VK_FILTER_LINEAR;
		si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		si.minLod = 0.0f;
		si.maxLod = 1.0f;
		si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		if (vkCreateSampler(m_device, &si, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_WARN(Render, "[SkillIconCache] vkCreateSampler a echoue — icones desactivees");
			m_sampler = VK_NULL_HANDLE;
			return false;
		}
		LOG_INFO(Render, "[SkillIconCache] Init OK (max={})", kMaxIcons);
		return true;
	}

	void SkillIconCache::Shutdown()
	{
#if defined(_WIN32)
		for (VkDescriptorSet ds : m_descriptors)
		{
			if (ds != VK_NULL_HANDLE)
			{
				ImGui_ImplVulkan_RemoveTexture(ds);
			}
		}
#endif
		m_descriptors.clear();
		m_cache.clear();
		if (m_sampler != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_device, m_sampler, nullptr);
		}
		m_sampler = VK_NULL_HANDLE;
		m_device = VK_NULL_HANDLE;
		m_registry = nullptr;
	}

	uint64_t SkillIconCache::GetOrLoad(const std::string& relPath)
	{
		if (relPath.empty() || m_sampler == VK_NULL_HANDLE || m_registry == nullptr)
		{
			return 0;
		}

		const auto it = m_cache.find(relPath);
		if (it != m_cache.end())
		{
			return it->second; // succes OU echec deja memorise (0).
		}

		// Plafond atteint : on ne charge plus (repli texte), sans memoriser pour
		// laisser une chance si de la place se libere (pas d'eviction en V1).
		if (m_cache.size() >= kMaxIcons)
		{
			return 0;
		}

#if defined(_WIN32)
		engine::render::TextureHandle handle = m_registry->LoadTexture(relPath, /*useSrgb=*/true);
		if (!handle.IsValid())
		{
			m_cache[relPath] = 0; // fichier absent/illisible — memorise l'echec.
			return 0;
		}
		engine::render::TextureAsset* tex = handle.Get();
		if (tex == nullptr || tex->view == VK_NULL_HANDLE)
		{
			m_cache[relPath] = 0;
			return 0;
		}
		VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(m_sampler, tex->view,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (ds == VK_NULL_HANDLE)
		{
			m_cache[relPath] = 0;
			return 0;
		}
		m_descriptors.push_back(ds);
		const uint64_t id = reinterpret_cast<uint64_t>(ds);
		m_cache[relPath] = id;
		return id;
#else
		return 0;
#endif
	}
}
