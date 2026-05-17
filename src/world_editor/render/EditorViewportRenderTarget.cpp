#include "src/world_editor/render/EditorViewportRenderTarget.h"

#include "src/shared/core/Log.h"

#include <functional>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#	include "imgui_impl_vulkan.h"
#endif

namespace engine::editor::world
{
	namespace
	{
		/// Choisit un MemoryType DEVICE_LOCAL pour l'image offscreen.
		/// Pattern miroir de `TexturePreviewCache::FindMemoryType`.
		bool PickMemoryTypeIndex(VkPhysicalDevice physDev,
			uint32_t memoryTypeBits, VkMemoryPropertyFlags wantedProps,
			uint32_t& outIndex)
		{
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				const bool typeMatches  = (memoryTypeBits & (1u << i)) != 0u;
				const bool propsOk      = (memProps.memoryTypes[i].propertyFlags
					& wantedProps) == wantedProps;
				if (typeMatches && propsOk)
				{
					outIndex = i;
					return true;
				}
			}
			return false;
		}

		/// Submit synchrone d'un command buffer one-shot + wait queue idle.
		/// Pattern partagé avec `TexturePreviewCache` / `TerrainEditingTools`
		/// pour les transitions d'image hors render loop.
		bool RunOneShotCommands(VkDevice device, VkQueue queue,
			uint32_t queueFamilyIndex, VkCommandBuffer& outPersistedCmd,
			VkCommandPool& outPersistedPool,
			const std::function<void(VkCommandBuffer)>& recordFn)
		{
			VkCommandPoolCreateInfo pci{};
			pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pci.queueFamilyIndex = queueFamilyIndex;
			if (vkCreateCommandPool(device, &pci, nullptr, &outPersistedPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[EditorViewportRT] vkCreateCommandPool failed");
				return false;
			}
			VkCommandBufferAllocateInfo aci{};
			aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			aci.commandPool = outPersistedPool;
			aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			aci.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(device, &aci, &outPersistedCmd) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, outPersistedPool, nullptr);
				outPersistedPool = VK_NULL_HANDLE;
				LOG_ERROR(Render, "[EditorViewportRT] vkAllocateCommandBuffers failed");
				return false;
			}
			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if (vkBeginCommandBuffer(outPersistedCmd, &bi) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, outPersistedPool, nullptr);
				outPersistedPool = VK_NULL_HANDLE;
				outPersistedCmd  = VK_NULL_HANDLE;
				return false;
			}
			recordFn(outPersistedCmd);
			if (vkEndCommandBuffer(outPersistedCmd) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, outPersistedPool, nullptr);
				outPersistedPool = VK_NULL_HANDLE;
				outPersistedCmd  = VK_NULL_HANDLE;
				return false;
			}
			VkSubmitInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			si.commandBufferCount = 1;
			si.pCommandBuffers    = &outPersistedCmd;
			if (vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, outPersistedPool, nullptr);
				outPersistedPool = VK_NULL_HANDLE;
				outPersistedCmd  = VK_NULL_HANDLE;
				return false;
			}
			vkQueueWaitIdle(queue);
			vkDestroyCommandPool(device, outPersistedPool, nullptr);
			outPersistedPool = VK_NULL_HANDLE;
			outPersistedCmd  = VK_NULL_HANDLE;
			return true;
		}
	}

	EditorViewportRenderTarget::~EditorViewportRenderTarget()
	{
		// Destruction explicite par l'appelant via Shutdown(device) — pas de
		// VkDevice ici donc on log un warn si encore valide (équivaut au
		// pattern WorldEditorImGui qui prévient l'oubli de Shutdown).
		if (m_image != VK_NULL_HANDLE || m_sampler != VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[EditorViewportRT] destructor : Shutdown(device) non appele avant destruction");
		}
	}

	bool EditorViewportRenderTarget::Init(VkDevice device, VkPhysicalDevice physicalDevice,
		VkQueue queue, uint32_t queueFamilyIndex,
		uint32_t initialWidth, uint32_t initialHeight)
	{
		if (m_image != VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[EditorViewportRT] Init appele deux fois sans Shutdown intermediaire");
			return false;
		}
		if (initialWidth == 0u || initialHeight == 0u)
		{
			LOG_ERROR(Render, "[EditorViewportRT] Init avec taille nulle ({}x{})",
				initialWidth, initialHeight);
			return false;
		}

		// Sampler persistant : reutilise entre Resize.
		VkSamplerCreateInfo sci{};
		sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter    = VK_FILTER_LINEAR;
		sci.minFilter    = VK_FILTER_LINEAR;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sci.maxLod       = 0.0f;
		if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[EditorViewportRT] vkCreateSampler failed");
			return false;
		}

		if (!CreateImageResources(device, physicalDevice, queue, queueFamilyIndex,
			initialWidth, initialHeight))
		{
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO(Render, "[EditorViewportRT] Init OK ({}x{} R8G8B8A8_UNORM)",
			initialWidth, initialHeight);
		return true;
	}

	void EditorViewportRenderTarget::Shutdown(VkDevice device)
	{
		DestroyImageResources(device);
		if (m_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
		}
		m_width  = 0;
		m_height = 0;
	}

	bool EditorViewportRenderTarget::Resize(VkDevice device, VkPhysicalDevice physicalDevice,
		VkQueue queue, uint32_t queueFamilyIndex,
		uint32_t newWidth, uint32_t newHeight)
	{
		if (newWidth == m_width && newHeight == m_height) return true;
		if (newWidth == 0u || newHeight == 0u) return false;
		DestroyImageResources(device);
		return CreateImageResources(device, physicalDevice, queue, queueFamilyIndex,
			newWidth, newHeight);
	}

	uint64_t EditorViewportRenderTarget::GetImguiTextureId() const
	{
		return m_imguiTextureId;
	}

	void EditorViewportRenderTarget::DestroyImageResources(VkDevice device)
	{
#if defined(_WIN32)
		if (m_imguiTextureId != 0u)
		{
			ImGui_ImplVulkan_RemoveTexture(
				reinterpret_cast<VkDescriptorSet>(m_imguiTextureId));
			m_imguiTextureId = 0u;
		}
#else
		m_imguiTextureId = 0u;
#endif
		if (m_view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_view, nullptr);
			m_view = VK_NULL_HANDLE;
		}
		if (m_image != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
		}
		if (m_memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
		}
	}

	bool EditorViewportRenderTarget::CreateImageResources(VkDevice device,
		VkPhysicalDevice physicalDevice, VkQueue queue, uint32_t queueFamilyIndex,
		uint32_t w, uint32_t h)
	{
		// 1) Image — usage TRANSFER_DST (PR 2 copiera SceneColor_LDR dedans)
		//    + SAMPLED (ImGui::Image sample dedans) + COLOR_ATTACHMENT
		//    (optionnel : permet aussi un rendu direct si la PR 3 utilise
		//    cette image comme attachment).
		VkImageCreateInfo ici{};
		ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType     = VK_IMAGE_TYPE_2D;
		ici.format        = kFormat;
		ici.extent        = { w, h, 1u };
		ici.mipLevels     = 1u;
		ici.arrayLayers   = 1u;
		ici.samples       = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
		ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT
		                  | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		                  | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[EditorViewportRT] vkCreateImage failed ({}x{})", w, h);
			return false;
		}

		// 2) Memory
		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device, m_image, &memReq);
		uint32_t memTypeIdx = 0u;
		if (!PickMemoryTypeIndex(physicalDevice, memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memTypeIdx))
		{
			LOG_ERROR(Render, "[EditorViewportRT] no DEVICE_LOCAL memory type");
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			return false;
		}
		VkMemoryAllocateInfo mai{};
		mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize  = memReq.size;
		mai.memoryTypeIndex = memTypeIdx;
		if (vkAllocateMemory(device, &mai, nullptr, &m_memory) != VK_SUCCESS
			|| vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[EditorViewportRT] vkAllocate/BindMemory failed");
			if (m_memory != VK_NULL_HANDLE) vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image  = VK_NULL_HANDLE;
			m_memory = VK_NULL_HANDLE;
			return false;
		}

		// 3) Transition UNDEFINED → SHADER_READ_ONLY_OPTIMAL — sinon
		//    ImGui::Image sample une image en layout invalide → validation
		//    layer warn + comportement indéfini.
		VkCommandBuffer cmd  = VK_NULL_HANDLE;
		VkCommandPool   pool = VK_NULL_HANDLE;
		const bool transitionOk = RunOneShotCommands(device, queue, queueFamilyIndex,
			cmd, pool,
			[&](VkCommandBuffer c)
			{
				VkImageMemoryBarrier b{};
				b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
				b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				b.srcAccessMask = 0;
				b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image            = m_image;
				b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdPipelineBarrier(c,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &b);
			});
		if (!transitionOk)
		{
			LOG_ERROR(Render, "[EditorViewportRT] transition layout failed");
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image  = VK_NULL_HANDLE;
			m_memory = VK_NULL_HANDLE;
			return false;
		}

		// 4) View
		VkImageViewCreateInfo vci{};
		vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image            = m_image;
		vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
		vci.format           = kFormat;
		vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &vci, nullptr, &m_view) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[EditorViewportRT] vkCreateImageView failed");
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image  = VK_NULL_HANDLE;
			m_memory = VK_NULL_HANDLE;
			return false;
		}

		// 5) Descriptor ImGui (Windows uniquement — l'editeur monde aussi).
#if defined(_WIN32)
		VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(m_sampler, m_view,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (ds == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[EditorViewportRT] ImGui_ImplVulkan_AddTexture failed");
			vkDestroyImageView(device, m_view, nullptr);
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			m_image  = VK_NULL_HANDLE;
			m_memory = VK_NULL_HANDLE;
			m_view   = VK_NULL_HANDLE;
			return false;
		}
		m_imguiTextureId = reinterpret_cast<uint64_t>(ds);
#else
		(void)queue; (void)queueFamilyIndex;
		// Hors Windows : pas d'ImGui Vulkan disponible. L'éditeur monde est
		// Windows-only ; la classe reste compilable pour engine_core mais
		// `GetImguiTextureId()` retourne 0 → `ImGui::Image` ne dessine rien.
		m_imguiTextureId = 0u;
#endif

		m_width  = w;
		m_height = h;
		return true;
	}
}
