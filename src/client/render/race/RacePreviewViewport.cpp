#include "src/client/render/race/RacePreviewViewport.h"

#include "src/shared/core/Log.h"

#include <functional>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#	include "imgui_impl_vulkan.h"
#endif

namespace engine::render::race
{
	namespace
	{
		/// Constante 2pi pour le wrap mod 2pi de l'angle orbit dans Tick.
		/// Definie localement pour eviter de tirer <numbers> (C++20) et
		/// garder la dependance reduite a vulkan + Log.
		constexpr float k2Pi = 6.28318530717958647692f;

		/// Choisit un MemoryType DEVICE_LOCAL pour l'image offscreen.
		/// Pattern miroir de `EditorViewportRenderTarget::PickMemoryTypeIndex`.
		/// \param physDev physical device a interroger.
		/// \param memoryTypeBits masque des types compatibles (renvoye par
		///        vkGetImageMemoryRequirements).
		/// \param wantedProps flags de propertyFlags requis (typiquement
		///        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).
		/// \param outIndex sortie : index du memory type retenu.
		/// \return true si un type a ete trouve.
		bool PickMemoryTypeIndex(VkPhysicalDevice physDev,
		                         uint32_t memoryTypeBits,
		                         VkMemoryPropertyFlags wantedProps,
		                         uint32_t& outIndex)
		{
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				const bool typeMatches = (memoryTypeBits & (1u << i)) != 0u;
				const bool propsOk     = (memProps.memoryTypes[i].propertyFlags
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
		/// Pattern partage avec `EditorViewportRenderTarget` pour les
		/// transitions d'image hors render loop. Le pool est detruit avant
		/// retour : aucun handle persiste cote appelant.
		///
		/// \param recordFn callback qui enregistre les commandes dans le
		///        command buffer fourni (deja en `vkBeginCommandBuffer`).
		/// \return true si le submit a reussi (et que vkQueueWaitIdle a
		///         pu etre appele sans erreur de retour).
		///
		/// Effets de bord : appelle vkQueueWaitIdle — bloque le thread
		/// jusqu'a la fin d'execution sur la queue.
		bool RunOneShotCommands(VkDevice device, VkQueue queue,
		                        uint32_t queueFamilyIndex,
		                        const std::function<void(VkCommandBuffer)>& recordFn)
		{
			VkCommandPool   pool = VK_NULL_HANDLE;
			VkCommandBuffer cmd  = VK_NULL_HANDLE;

			VkCommandPoolCreateInfo pci{};
			pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pci.queueFamilyIndex = queueFamilyIndex;
			if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[RacePreviewViewport] vkCreateCommandPool failed");
				return false;
			}

			VkCommandBufferAllocateInfo aci{};
			aci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			aci.commandPool        = pool;
			aci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			aci.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(device, &aci, &cmd) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[RacePreviewViewport] vkAllocateCommandBuffers failed");
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[RacePreviewViewport] vkBeginCommandBuffer failed");
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			recordFn(cmd);

			if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[RacePreviewViewport] vkEndCommandBuffer failed");
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			VkSubmitInfo si{};
			si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			si.commandBufferCount = 1;
			si.pCommandBuffers    = &cmd;
			if (vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[RacePreviewViewport] vkQueueSubmit failed");
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			vkQueueWaitIdle(queue);
			vkDestroyCommandPool(device, pool, nullptr);
			return true;
		}
	}

	RacePreviewViewport::~RacePreviewViewport()
	{
		// Destruction explicite par l'appelant via Shutdown(device) — pas
		// de VkDevice ici donc on warn si encore valide. Memes raisons que
		// EditorViewportRenderTarget : on ne peut pas appeler vkDestroy*
		// sans device handle, et le risque est qu'on laisse fuiter les
		// ressources si l'appelant a oublie Shutdown.
		if (m_image != VK_NULL_HANDLE || m_sampler != VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[RacePreviewViewport] destructor : Shutdown(device) non appele avant destruction");
		}
	}

	bool RacePreviewViewport::Init(VkDevice device, VkPhysicalDevice physicalDevice,
	                               VkQueue queue, uint32_t queueFamilyIndex,
	                               uint32_t width, uint32_t height)
	{
		if (m_image != VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[RacePreviewViewport] Init appele deux fois sans Shutdown intermediaire");
			return false;
		}
		if (width == 0u || height == 0u)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] Init avec taille nulle ({}x{})", width, height);
			return false;
		}

		// 1) Sampler — linear filter + clamp-to-edge, conforme au pattern
		//    EditorViewportRenderTarget.
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
			LOG_ERROR(Render, "[RacePreviewViewport] vkCreateSampler failed");
			return false;
		}

		// 2) Image — usage : SAMPLED (ImGui::Image en lit), TRANSFER_DST
		//    (Render fait un vkCmdClearColorImage en Task 9, et copy ou
		//    blit en Task 11), COLOR_ATTACHMENT (Task 11 fera un rendu
		//    skinned direct dedans).
		VkImageCreateInfo ici{};
		ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType     = VK_IMAGE_TYPE_2D;
		ici.format        = kFormat;
		ici.extent        = { width, height, 1u };
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
			LOG_ERROR(Render, "[RacePreviewViewport] vkCreateImage failed ({}x{})", width, height);
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
			return false;
		}

		// 3) Memory — DEVICE_LOCAL pour la perf de sampling.
		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device, m_image, &memReq);
		uint32_t memTypeIdx = 0u;
		if (!PickMemoryTypeIndex(physicalDevice, memReq.memoryTypeBits,
		                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memTypeIdx))
		{
			LOG_ERROR(Render, "[RacePreviewViewport] no DEVICE_LOCAL memory type");
			vkDestroyImage(device, m_image, nullptr);
			vkDestroySampler(device, m_sampler, nullptr);
			m_image   = VK_NULL_HANDLE;
			m_sampler = VK_NULL_HANDLE;
			return false;
		}
		VkMemoryAllocateInfo mai{};
		mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize  = memReq.size;
		mai.memoryTypeIndex = memTypeIdx;
		if (vkAllocateMemory(device, &mai, nullptr, &m_memory) != VK_SUCCESS
			|| vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] vkAllocate/BindMemory failed");
			if (m_memory != VK_NULL_HANDLE) vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			vkDestroySampler(device, m_sampler, nullptr);
			m_image   = VK_NULL_HANDLE;
			m_memory  = VK_NULL_HANDLE;
			m_sampler = VK_NULL_HANDLE;
			return false;
		}

		// 4) Transition UNDEFINED → SHADER_READ_ONLY_OPTIMAL — sinon
		//    ImGui::Image sample une image en layout invalide. Identique
		//    a EditorViewportRenderTarget : on accepte que l'image soit
		//    noire au demarrage (defaut implementation des drivers pour
		//    UNDEFINED→SHADER_READ_ONLY).
		const bool transitionOk = RunOneShotCommands(device, queue, queueFamilyIndex,
			[&](VkCommandBuffer c)
			{
				VkImageMemoryBarrier b{};
				b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
				b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				b.srcAccessMask       = 0;
				b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image               = m_image;
				b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdPipelineBarrier(c,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &b);
			});
		if (!transitionOk)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] transition layout failed");
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			vkDestroySampler(device, m_sampler, nullptr);
			m_image   = VK_NULL_HANDLE;
			m_memory  = VK_NULL_HANDLE;
			m_sampler = VK_NULL_HANDLE;
			return false;
		}

		// 5) View
		VkImageViewCreateInfo vci{};
		vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image            = m_image;
		vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
		vci.format           = kFormat;
		vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &vci, nullptr, &m_view) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] vkCreateImageView failed");
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			vkDestroySampler(device, m_sampler, nullptr);
			m_image   = VK_NULL_HANDLE;
			m_memory  = VK_NULL_HANDLE;
			m_sampler = VK_NULL_HANDLE;
			return false;
		}

		// 6) Descriptor ImGui (Windows uniquement — comme
		//    EditorViewportRenderTarget). Hors Windows, GetImguiTextureId
		//    retourne 0 et ImGui::Image ne dessine rien.
#if defined(_WIN32)
		VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(m_sampler, m_view,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (ds == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] ImGui_ImplVulkan_AddTexture failed");
			vkDestroyImageView(device, m_view, nullptr);
			vkFreeMemory(device, m_memory, nullptr);
			vkDestroyImage(device, m_image, nullptr);
			vkDestroySampler(device, m_sampler, nullptr);
			m_image   = VK_NULL_HANDLE;
			m_memory  = VK_NULL_HANDLE;
			m_view    = VK_NULL_HANDLE;
			m_sampler = VK_NULL_HANDLE;
			return false;
		}
		m_imguiTextureId = reinterpret_cast<uint64_t>(ds);
#else
		(void)queue; (void)queueFamilyIndex;
		m_imguiTextureId = 0u;
#endif

		m_width  = width;
		m_height = height;
		LOG_INFO(Render, "[RacePreviewViewport] Init OK ({}x{} R8G8B8A8_UNORM)", width, height);
		return true;
	}

	void RacePreviewViewport::Shutdown(VkDevice device)
	{
		// Ordre destruction = ordre inverse de creation. Idempotent :
		// chaque champ est verifie contre VK_NULL_HANDLE avant destroy.
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
		if (m_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
		}
		m_width        = 0;
		m_height       = 0;
		m_currentMesh  = nullptr;
		m_orbitYawRad  = 0.0f;
	}

	void RacePreviewViewport::SetMesh(engine::render::skinned::SkinnedMesh* mesh)
	{
		// Pointer non-owning : on accepte nullptr (Render fera juste un
		// clear noir dans ce cas).
		m_currentMesh = mesh;
	}

	void RacePreviewViewport::Tick(float dt)
	{
		// Accumule l'angle orbit, puis wrap mod 2pi pour eviter
		// l'accumulation flottante sur de longues sessions. La valeur
		// n'est pas encore utilisee en Task 9 (Render fait juste un
		// clear noir) ; sera consommee par Render en Task 11.
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
		m_orbitYawRad += kOrbitDegPerSec * dt * kDegToRad;
		while (m_orbitYawRad >= k2Pi) m_orbitYawRad -= k2Pi;
		while (m_orbitYawRad <  0.0f) m_orbitYawRad += k2Pi;
	}

	void RacePreviewViewport::Render(VkCommandBuffer cmdBuf)
	{
		// Task 9 skeleton : clear noir uniquement. Pas de rendu skinned
		// — Task 11 ajoutera un mini-pipeline graphique + camera orbit
		// (utilisant m_orbitYawRad accumule par Tick) + sampling Idle.
		//
		// Sequence : SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL,
		// vkCmdClearColorImage, TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
		// On laisse l'image en SHADER_READ_ONLY_OPTIMAL a la sortie
		// pour que ImGui::Image puisse la sampler dans la passe suivante.
		if (m_image == VK_NULL_HANDLE) return;

		// Barrier 1 : SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL.
		{
			VkImageMemoryBarrier b{};
			b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			b.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
			b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image               = m_image;
			b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(cmdBuf,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &b);
		}

		// Clear color : noir opaque. Task 11 remplacera ce clear par
		// un rendu skinned (vkCmdBeginRendering + bind pipeline + draw).
		VkClearColorValue clearColor{};
		clearColor.float32[0] = 0.0f;
		clearColor.float32[1] = 0.0f;
		clearColor.float32[2] = 0.0f;
		clearColor.float32[3] = 1.0f;
		VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(cmdBuf, m_image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearColor, 1, &range);

		// Barrier 2 : TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
		{
			VkImageMemoryBarrier b{};
			b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
			b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image               = m_image;
			b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(cmdBuf,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &b);
		}

		// m_currentMesh est volontairement non utilise en Task 9 — le
		// rendu skinned arrive en Task 11.
		(void)m_currentMesh;
	}
}
