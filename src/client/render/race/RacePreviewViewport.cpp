#include "src/client/render/race/RacePreviewViewport.h"

#include "src/client/render/skinned/AnimationSampler.h"
#include "src/client/render/skinned/AvatarMaterialRouting.h"
#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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

		/// Crée un VkBuffer + VkDeviceMemory host-visible/coherent (sans données
		/// initiales) pour le bone SSBO et l'instance buffer, réécrits chaque
		/// RenderOffscreen. Pattern miroir de SkinnedRenderer::CreateEmptyHostVisibleBuffer.
		/// \param usage  STORAGE_BUFFER_BIT (SSBO) ou VERTEX_BUFFER_BIT (instance).
		/// \param bytes  Taille en octets.
		/// \param outBuf / outMem  Handles de sortie (VK_NULL_HANDLE en cas d'échec).
		/// \return true si création + allocation + bind ont réussi.
		bool CreateEmptyHostVisibleBuffer(VkDevice device, VkPhysicalDevice phys,
		                                  VkBufferUsageFlags usage, VkDeviceSize bytes,
		                                  VkBuffer* outBuf, VkDeviceMemory* outMem)
		{
			*outBuf = VK_NULL_HANDLE;
			*outMem = VK_NULL_HANDLE;
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = bytes;
			bi.usage       = usage;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bi, nullptr, outBuf) != VK_SUCCESS)
			{
				*outBuf = VK_NULL_HANDLE;
				return false;
			}
			VkMemoryRequirements mr{};
			vkGetBufferMemoryRequirements(device, *outBuf, &mr);
			uint32_t memIdx = 0u;
			if (!PickMemoryTypeIndex(phys, mr.memoryTypeBits,
			        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memIdx))
			{
				vkDestroyBuffer(device, *outBuf, nullptr);
				*outBuf = VK_NULL_HANDLE;
				return false;
			}
			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = mr.size;
			ai.memoryTypeIndex = memIdx;
			if (vkAllocateMemory(device, &ai, nullptr, outMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, *outBuf, nullptr);
				*outBuf = VK_NULL_HANDLE;
				*outMem = VK_NULL_HANDLE;
				return false;
			}
			if (vkBindBufferMemory(device, *outBuf, *outMem, 0) != VK_SUCCESS)
			{
				vkFreeMemory(device, *outMem, nullptr);
				vkDestroyBuffer(device, *outBuf, nullptr);
				*outBuf = VK_NULL_HANDLE;
				*outMem = VK_NULL_HANDLE;
				return false;
			}
			return true;
		}

		/// Construit une matrice de vue (world->view) regardant `target` depuis
		/// `eye`, convention left-handed +Z forward de l'engine — IDENTIQUE à
		/// `Camera::ComputeViewMatrix` (basis stockés en lignes, column-major) :
		///   right   = normalize(cross(forward, worldUp))
		///   up      = normalize(cross(right, forward))
		///   forward = normalize(target - eye)
		/// PerspectiveVulkan attend +Z forward (row3 = (0,0,1,0) -> clip.w = view.z).
		engine::math::Mat4 LookAtLH(const engine::math::Vec3& eye, const engine::math::Vec3& target)
		{
			engine::math::Vec3 forward = (target - eye).Normalized();
			// right = cross(forward, worldUp=(0,1,0)) = (-forward.z, 0, forward.x).
			engine::math::Vec3 right(-forward.z, 0.0f, forward.x);
			float rlen = right.Length();
			right = (rlen > 0.0f) ? right * (1.0f / rlen) : engine::math::Vec3(1.0f, 0.0f, 0.0f);
			// up = cross(right, forward).
			engine::math::Vec3 up(
				right.y * forward.z - right.z * forward.y,
				right.z * forward.x - right.x * forward.z,
				right.x * forward.y - right.y * forward.x);
			float ulen = up.Length();
			up = (ulen > 0.0f) ? up * (1.0f / ulen) : engine::math::Vec3(0.0f, 1.0f, 0.0f);

			engine::math::Mat4 V;
			V.m[0] = right.x;   V.m[1] = up.x;     V.m[2]  = forward.x; V.m[3]  = 0.0f;
			V.m[4] = right.y;   V.m[5] = up.y;     V.m[6]  = forward.y; V.m[7]  = 0.0f;
			V.m[8] = right.z;   V.m[9] = up.z;     V.m[10] = forward.z; V.m[11] = 0.0f;
			V.m[12] = -(right.x   * eye.x + right.y   * eye.y + right.z   * eye.z);
			V.m[13] = -(up.x      * eye.x + up.y      * eye.y + up.z      * eye.z);
			V.m[14] = -(forward.x * eye.x + forward.y * eye.y + forward.z * eye.z);
			V.m[15] = 1.0f;
			return V;
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

		// Phase 2 — mémorise le contexte Vulkan pour le rendu autonome
		// (RenderOffscreen soumet ses propres command buffers one-shot).
		m_device           = device;
		m_physicalDevice   = physicalDevice;
		m_queue            = queue;
		m_queueFamilyIndex = queueFamilyIndex;

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

		// 4) Transition UNDEFINED → TRANSFER_DST → clear couleur explicite
		//    → SHADER_READ_ONLY_OPTIMAL. Sans le clear explicite, le contenu
		//    de l'image apres allocation est UNDEFINED cote Vulkan (souvent
		//    rendu invisible/transparent par le driver), ce qui se traduit
		//    par "rien du tout pas d'image" cote utilisateur dans
		//    AuthImGuiCharacterCreate (Render() n'est pas hooke dans la
		//    frame loop en MVP, donc l'image n'est jamais re-clearee apres
		//    Init). Le clear donne un fond bleu sombre visible — l'overlay
		//    ImGui::Text("Race : ...") par-dessus reste l'element informatif.
		const bool transitionOk = RunOneShotCommands(device, queue, queueFamilyIndex,
			[&](VkCommandBuffer c)
			{
				// 4a) UNDEFINED → TRANSFER_DST pour pouvoir clear.
				{
					VkImageMemoryBarrier b{};
					b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
					b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b.srcAccessMask       = 0;
					b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
					b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.image               = m_image;
					b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					vkCmdPipelineBarrier(c,
						VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
				}

				// 4b) Clear : bleu sombre (0.10, 0.12, 0.18, 1.0). Aligne sur la
				//     couleur "mesh attache" de Render() pour coherence visuelle.
				VkClearColorValue clearColor{};
				clearColor.float32[0] = 0.10f;
				clearColor.float32[1] = 0.12f;
				clearColor.float32[2] = 0.18f;
				clearColor.float32[3] = 1.00f;
				VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdClearColorImage(c, m_image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					&clearColor, 1, &range);

				// 4c) TRANSFER_DST → SHADER_READ_ONLY_OPTIMAL pour que
				//     ImGui::Image puisse sampler l'image.
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
					vkCmdPipelineBarrier(c,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
				}
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

		// --- Phase 2 : ressources du pipeline forward (avant l'image/sampler) ---
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
		if (m_framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, m_framebuffer, nullptr);
			m_framebuffer = VK_NULL_HANDLE;
		}
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}
		// Le descriptor set bone est libéré par la destruction du pool.
		if (m_boneDescPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_boneDescPool, nullptr);
			m_boneDescPool = VK_NULL_HANDLE;
		}
		m_boneDescSet = VK_NULL_HANDLE;
		if (m_boneSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_boneSetLayout, nullptr);
			m_boneSetLayout = VK_NULL_HANDLE;
		}
		if (m_boneBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_boneBuffer, nullptr);
			m_boneBuffer = VK_NULL_HANDLE;
		}
		if (m_boneBufferMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_boneBufferMemory, nullptr);
			m_boneBufferMemory = VK_NULL_HANDLE;
		}
		if (m_instanceBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_instanceBuffer, nullptr);
			m_instanceBuffer = VK_NULL_HANDLE;
		}
		if (m_instanceBufferMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_instanceBufferMemory, nullptr);
			m_instanceBufferMemory = VK_NULL_HANDLE;
		}
		if (m_depthImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_depthImageView, nullptr);
			m_depthImageView = VK_NULL_HANDLE;
		}
		if (m_depthImage != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, m_depthImage, nullptr);
			m_depthImage = VK_NULL_HANDLE;
		}
		if (m_depthImageMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_depthImageMemory, nullptr);
			m_depthImageMemory = VK_NULL_HANDLE;
		}
		m_materialDescSet = VK_NULL_HANDLE;

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
		m_localBoneMatrices.clear();
		m_globalBoneMatrices.clear();
		m_finalBoneMatrices.clear();
		m_sampleStartSec = 0.0f;
		m_device           = VK_NULL_HANDLE;
		m_physicalDevice   = VK_NULL_HANDLE;
		m_queue            = VK_NULL_HANDLE;
		m_queueFamilyIndex = 0u;
	}

	void RacePreviewViewport::SetMesh(engine::render::skinned::SkinnedMesh* mesh)
	{
		// Pointer non-owning : on accepte nullptr (Render fera juste un
		// clear noir dans ce cas). On reinitialise l'etat de sampling pour
		// eviter de melanger les matrices echantillonnees du mesh precedent
		// avec le squelette du nouveau mesh (tailles potentiellement
		// differentes). m_sampleStartSec est remis a 0 pour que Tick
		// recalibre nowSec au prochain Tick avec mesh dispo : l'anim
		// recommence proprement a t=0.
		m_currentMesh = mesh;
		m_localBoneMatrices.clear();
		m_globalBoneMatrices.clear();
		m_finalBoneMatrices.clear();
		m_sampleStartSec = 0.0f;
	}

	void RacePreviewViewport::Tick(float dt)
	{
		// Accumule l'angle orbit, puis wrap mod 2pi pour eviter
		// l'accumulation flottante sur de longues sessions.
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
		// Rotation orbit automatique seulement si activée (écran de création). La
		// fenêtre Personnage la coupe et pilote l'angle via SetOrbitYaw (drag).
		if (m_autoOrbit)
			m_orbitYawRad += kOrbitDegPerSec * dt * kDegToRad;
		while (m_orbitYawRad >= k2Pi) m_orbitYawRad -= k2Pi;
		while (m_orbitYawRad <  0.0f) m_orbitYawRad += k2Pi;

		// Pas de mesh attache ou aucun clip d'anim : on vide les buffers
		// de matrices pour signaler "rien a rendre" a Render (qui choisira
		// alors le clear noir au lieu du tint bleu sombre).
		if (m_currentMesh == nullptr || m_currentMesh->clips.empty())
		{
			m_localBoneMatrices.clear();
			m_globalBoneMatrices.clear();
			m_finalBoneMatrices.clear();
			return;
		}

		// Cherche le clip "Idle" (Mixamo : pose statique en bouclage). Si
		// absent, fallback sur le premier clip disponible (probablement
		// Walk ou Run) pour conserver un retour visuel anime meme si la
		// race n'a pas exporte d'Idle.
		const engine::render::skinned::AnimationClip* idleClip = nullptr;
		for (const auto& c : m_currentMesh->clips)
		{
			if (c.name == "Idle")
			{
				idleClip = &c;
				break;
			}
		}
		if (idleClip == nullptr) idleClip = &m_currentMesh->clips.front();

		// Clip vide / mal forme : on vide les buffers pour eviter une
		// division par 0 dans fmod et signaler "pas d'anim" a Render.
		if (idleClip->duration <= 0.0f)
		{
			m_localBoneMatrices.clear();
			m_globalBoneMatrices.clear();
			m_finalBoneMatrices.clear();
			return;
		}

		// Init paresseuse du temps de reference : on capture nowSec au 1er
		// Tick ou un mesh est dispo, pour que l'anim demarre a t=0 plutot
		// que de "sauter" au milieu du clip si l'utilisateur entre dans
		// l'ecran de creation longtemps apres le boot.
		const float nowSec = static_cast<float>(
			std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		if (m_sampleStartSec == 0.0f)
		{
			m_sampleStartSec = nowSec;
		}
		const float elapsed = nowSec - m_sampleStartSec;
		const float t       = std::fmod(std::max(0.0f, elapsed), idleClip->duration);

		// Pipeline en trois etapes (cf. AnimationSampler.h) :
		//   1) Sample TRS locales depuis les keyframes.
		//   2) Propage la hierarchie pour obtenir les globales.
		//   3) Multiplie par inverseBindGlobal pour les matrices finales
		//      consommables par un shader de skinning.
		// Task 11 MVP n'envoie pas encore ces matrices au GPU (cf. Render),
		// mais le pipeline est en place pour le futur refactor RT-agnostic.
		m_localBoneMatrices = engine::render::skinned::AnimationSampler::SamplePose(
			m_currentMesh->skeleton, *idleClip, t);
		m_globalBoneMatrices = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(
			m_currentMesh->skeleton, m_localBoneMatrices);
		m_finalBoneMatrices = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(
			m_currentMesh->skeleton, m_globalBoneMatrices);

		(void)dt;  // dt deja consomme par l'accumulation d'angle orbit ci-dessus.
	}

	void RacePreviewViewport::SetGender(const std::string& gender)
	{
		m_gender = (gender == "female") ? "female" : "male";
	}

	void RacePreviewViewport::SetSkinTone(int tone)
	{
		m_skinTone = (tone == 1) ? 1 : 0;
	}

	void RacePreviewViewport::SetAvatarMaterials(VkDescriptorSet materialSet, uint32_t outfitId,
	                                             uint32_t bodyMaleId, uint32_t bodyFemaleId,
	                                             uint32_t bodyMaleDarkId, uint32_t bodyFemaleDarkId,
	                                             const std::vector<std::string>& bodyNames,
	                                             float skinDepthBiasConstant, float skinDepthBiasSlope)
	{
		m_materialDescSet          = materialSet;
		m_outfitMaterialId         = outfitId;
		m_bodyMaterialIdMale       = bodyMaleId;
		m_bodyMaterialIdFemale     = bodyFemaleId;
		m_bodyMaterialIdMaleDark   = bodyMaleDarkId;
		m_bodyMaterialIdFemaleDark = bodyFemaleDarkId;
		m_bodyMaterialNames        = bodyNames;
		m_skinDepthBiasConstant    = skinDepthBiasConstant;
		m_skinDepthBiasSlope       = skinDepthBiasSlope;
	}

	bool RacePreviewViewport::InitForwardPipeline(VkDescriptorSetLayout materialLayout,
	                                              const uint32_t* vertSpirv, size_t vertWordCount,
	                                              const uint32_t* fragSpirv, size_t fragWordCount)
	{
		if (m_device == VK_NULL_HANDLE || !IsValid())
		{
			LOG_ERROR(Render, "[RacePreviewViewport] InitForwardPipeline avant Init reussi");
			return false;
		}
		if (materialLayout == VK_NULL_HANDLE || !vertSpirv || vertWordCount == 0
			|| !fragSpirv || fragWordCount == 0)
		{
			LOG_ERROR(Render, "[RacePreviewViewport] InitForwardPipeline : arguments invalides "
				"(materialLayout={} vertWords={} fragWords={})",
				(void*)materialLayout, vertWordCount, fragWordCount);
			return false;
		}

		VkDevice device = m_device;

		// Cleanup partiel en cas d'echec : detruit uniquement les ressources
		// forward creees jusque-la (laisse l'image/sampler de Init intacts pour
		// le fallback clear). Remet m_pipeline a NULL -> IsForwardReady() == false.
		auto fail = [&](const char* msg) -> bool {
			LOG_ERROR(Render, "[RacePreviewViewport] InitForwardPipeline: {}", msg);
			if (m_pipeline != VK_NULL_HANDLE)       { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
			if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
			if (m_framebuffer != VK_NULL_HANDLE)    { vkDestroyFramebuffer(device, m_framebuffer, nullptr); m_framebuffer = VK_NULL_HANDLE; }
			if (m_renderPass != VK_NULL_HANDLE)     { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
			if (m_boneDescPool != VK_NULL_HANDLE)   { vkDestroyDescriptorPool(device, m_boneDescPool, nullptr); m_boneDescPool = VK_NULL_HANDLE; }
			m_boneDescSet = VK_NULL_HANDLE;
			if (m_boneSetLayout != VK_NULL_HANDLE)  { vkDestroyDescriptorSetLayout(device, m_boneSetLayout, nullptr); m_boneSetLayout = VK_NULL_HANDLE; }
			if (m_boneBuffer != VK_NULL_HANDLE)     { vkDestroyBuffer(device, m_boneBuffer, nullptr); m_boneBuffer = VK_NULL_HANDLE; }
			if (m_boneBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_boneBufferMemory, nullptr); m_boneBufferMemory = VK_NULL_HANDLE; }
			if (m_instanceBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, m_instanceBuffer, nullptr); m_instanceBuffer = VK_NULL_HANDLE; }
			if (m_instanceBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_instanceBufferMemory, nullptr); m_instanceBufferMemory = VK_NULL_HANDLE; }
			if (m_depthImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
			if (m_depthImage != VK_NULL_HANDLE)     { vkDestroyImage(device, m_depthImage, nullptr); m_depthImage = VK_NULL_HANDLE; }
			if (m_depthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_depthImageMemory, nullptr); m_depthImageMemory = VK_NULL_HANDLE; }
			return false;
		};

		// 1) Depth image + memory + view (D32_SFLOAT, DEPTH_STENCIL_ATTACHMENT).
		{
			VkImageCreateInfo ici{};
			ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType     = VK_IMAGE_TYPE_2D;
			ici.format        = kDepthFormat;
			ici.extent        = { m_width, m_height, 1u };
			ici.mipLevels     = 1u;
			ici.arrayLayers   = 1u;
			ici.samples       = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
			ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (vkCreateImage(device, &ici, nullptr, &m_depthImage) != VK_SUCCESS)
				return fail("vkCreateImage depth failed");

			VkMemoryRequirements memReq{};
			vkGetImageMemoryRequirements(device, m_depthImage, &memReq);
			uint32_t memTypeIdx = 0u;
			if (!PickMemoryTypeIndex(m_physicalDevice, memReq.memoryTypeBits,
			                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memTypeIdx))
				return fail("no DEVICE_LOCAL memory type (depth)");
			VkMemoryAllocateInfo mai{};
			mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize  = memReq.size;
			mai.memoryTypeIndex = memTypeIdx;
			if (vkAllocateMemory(device, &mai, nullptr, &m_depthImageMemory) != VK_SUCCESS
				|| vkBindImageMemory(device, m_depthImage, m_depthImageMemory, 0) != VK_SUCCESS)
				return fail("vkAllocate/Bind depth memory failed");

			VkImageViewCreateInfo vci{};
			vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vci.image            = m_depthImage;
			vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
			vci.format           = kDepthFormat;
			vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			if (vkCreateImageView(device, &vci, nullptr, &m_depthImageView) != VK_SUCCESS)
				return fail("vkCreateImageView depth failed");
		}

		// 2) Render pass : 1 color (clear -> SHADER_READ_ONLY) + 1 depth (clear).
		{
			VkAttachmentDescription att[2] = {};
			att[0].format         = kFormat;
			att[0].samples        = VK_SAMPLE_COUNT_1_BIT;
			att[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
			att[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			att[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			att[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			att[1].format         = kDepthFormat;
			att[1].samples        = VK_SAMPLE_COUNT_1_BIT;
			att[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
			att[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			att[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount    = 1;
			subpass.pColorAttachments       = &colorRef;
			subpass.pDepthStencilAttachment = &depthRef;

			VkSubpassDependency dep = {};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask = 0;
			dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			                  | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rpInfo = {};
			rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 2;
			rpInfo.pAttachments    = att;
			rpInfo.subpassCount    = 1;
			rpInfo.pSubpasses      = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies   = &dep;
			if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
				return fail("vkCreateRenderPass failed");
		}

		// 3) Framebuffer (color view de Init + depth view).
		{
			VkImageView fbViews[2] = { m_view, m_depthImageView };
			VkFramebufferCreateInfo fbi = {};
			fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbi.renderPass      = m_renderPass;
			fbi.attachmentCount = 2;
			fbi.pAttachments    = fbViews;
			fbi.width           = m_width;
			fbi.height          = m_height;
			fbi.layers          = 1;
			if (vkCreateFramebuffer(device, &fbi, nullptr, &m_framebuffer) != VK_SUCCESS)
				return fail("vkCreateFramebuffer failed");
		}

		// 4) Bone set layout (set 1, binding 0, STORAGE_BUFFER, VERTEX).
		{
			VkDescriptorSetLayoutBinding boneBinding = {};
			boneBinding.binding         = 0;
			boneBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			boneBinding.descriptorCount = 1;
			boneBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
			VkDescriptorSetLayoutCreateInfo ci = {};
			ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			ci.bindingCount = 1;
			ci.pBindings    = &boneBinding;
			if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &m_boneSetLayout) != VK_SUCCESS)
				return fail("bone set layout creation failed");
		}

		// 5) Pipeline layout : set 0 (materiau bindless) + set 1 (bones) + PC 144.
		{
			VkDescriptorSetLayout setLayouts[2] = { materialLayout, m_boneSetLayout };
			VkPushConstantRange pushRange = {};
			pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = 144u;
			VkPipelineLayoutCreateInfo li = {};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 2;
			li.pSetLayouts            = setLayouts;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pushRange;
			if (vkCreatePipelineLayout(device, &li, nullptr, &m_pipelineLayout) != VK_SUCCESS)
				return fail("vkCreatePipelineLayout failed");
		}

		// 6) Graphics pipeline (vertex input skinné identique a SkinnedRenderer).
		{
			VkShaderModule vertModule = VK_NULL_HANDLE;
			VkShaderModule fragModule = VK_NULL_HANDLE;
			VkShaderModuleCreateInfo mi = {};
			mi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			mi.pCode    = vertSpirv;
			mi.codeSize = vertWordCount * sizeof(uint32_t);
			if (vkCreateShaderModule(device, &mi, nullptr, &vertModule) != VK_SUCCESS)
				return fail("vertex shader module creation failed");
			mi.pCode    = fragSpirv;
			mi.codeSize = fragWordCount * sizeof(uint32_t);
			if (vkCreateShaderModule(device, &mi, nullptr, &fragModule) != VK_SUCCESS)
			{
				vkDestroyShaderModule(device, vertModule, nullptr);
				return fail("fragment shader module creation failed");
			}

			VkPipelineShaderStageCreateInfo stages[2] = {};
			stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertModule;
			stages[0].pName  = "main";
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName  = "main";

			// binding 0 = SkinnedVertex (stride 56), binding 1 = mat4 instance (64).
			VkVertexInputBindingDescription bindings[2] = {};
			bindings[0].binding = 0; bindings[0].stride = 56; bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			bindings[1].binding = 1; bindings[1].stride = 64; bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			VkVertexInputAttributeDescription attrs[9] = {};
			attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0 };
			attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 };
			attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 };
			attrs[3] = { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };
			attrs[4] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 };
			attrs[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 };
			attrs[6] = { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 };
			attrs[7] = { 7, 0, VK_FORMAT_R16G16B16A16_UINT,   32 };
			attrs[8] = { 8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40 };
			VkPipelineVertexInputStateCreateInfo vi = {};
			vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexBindingDescriptionCount   = 2;
			vi.pVertexBindingDescriptions      = bindings;
			vi.vertexAttributeDescriptionCount = 9;
			vi.pVertexAttributeDescriptions    = attrs;

			VkPipelineInputAssemblyStateCreateInfo ia = {};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp = {};
			vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			// CCW + BACK : winding glTF (cf. SkinnedRenderer / CLAUDE.md). NE PAS
			// passer en CLOCKWISE — meme mesh, meme convention que l'avatar in-world.
			VkPipelineRasterizationStateCreateInfo rs = {};
			rs.sType           = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode     = VK_POLYGON_MODE_FILL;
			rs.cullMode        = VK_CULL_MODE_BACK_BIT;
			rs.frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth       = 1.0f;
			rs.depthBiasEnable = VK_TRUE; // valeurs poussees dynamiquement (peau/habit)

			VkPipelineMultisampleStateCreateInfo ms = {};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds = {};
			ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = VK_TRUE;
			ds.depthWriteEnable = VK_TRUE;
			ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

			VkPipelineColorBlendAttachmentState blendAtt = {};
			blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo cb = {};
			cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments    = &blendAtt;

			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
			                               VK_DYNAMIC_STATE_DEPTH_BIAS };
			VkPipelineDynamicStateCreateInfo dyn = {};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 3;
			dyn.pDynamicStates    = dynStates;

			VkGraphicsPipelineCreateInfo gp = {};
			gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gp.stageCount          = 2;
			gp.pStages             = stages;
			gp.pVertexInputState   = &vi;
			gp.pInputAssemblyState = &ia;
			gp.pViewportState      = &vp;
			gp.pRasterizationState = &rs;
			gp.pMultisampleState   = &ms;
			gp.pDepthStencilState  = &ds;
			gp.pColorBlendState    = &cb;
			gp.pDynamicState       = &dyn;
			gp.layout              = m_pipelineLayout;
			gp.renderPass          = m_renderPass;
			gp.subpass             = 0;
			const VkResult pr = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vertModule, nullptr);
			vkDestroyShaderModule(device, fragModule, nullptr);
			if (pr != VK_SUCCESS)
				return fail("vkCreateGraphicsPipelines failed");
		}

		// 7) Bone SSBO (slot unique) + instance buffer.
		{
			const VkDeviceSize boneBytes = static_cast<VkDeviceSize>(kMaxBones) * sizeof(float) * 16;
			if (!CreateEmptyHostVisibleBuffer(device, m_physicalDevice,
			        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boneBytes, &m_boneBuffer, &m_boneBufferMemory))
				return fail("bone SSBO creation failed");
			if (!CreateEmptyHostVisibleBuffer(device, m_physicalDevice,
			        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 64u, &m_instanceBuffer, &m_instanceBufferMemory))
				return fail("instance buffer creation failed");
		}

		// 8) Descriptor pool + bone descriptor set pointant sur m_boneBuffer.
		{
			VkDescriptorPoolSize poolSize = {};
			poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSize.descriptorCount = 1;
			VkDescriptorPoolCreateInfo pci = {};
			pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pci.maxSets       = 1;
			pci.poolSizeCount = 1;
			pci.pPoolSizes    = &poolSize;
			if (vkCreateDescriptorPool(device, &pci, nullptr, &m_boneDescPool) != VK_SUCCESS)
				return fail("descriptor pool creation failed");

			VkDescriptorSetAllocateInfo ai = {};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = m_boneDescPool;
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &m_boneSetLayout;
			if (vkAllocateDescriptorSets(device, &ai, &m_boneDescSet) != VK_SUCCESS)
				return fail("bone descriptor set allocation failed");

			VkDescriptorBufferInfo bufInfo = {};
			bufInfo.buffer = m_boneBuffer;
			bufInfo.offset = 0;
			bufInfo.range  = VK_WHOLE_SIZE;
			VkWriteDescriptorSet write = {};
			write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet          = m_boneDescSet;
			write.dstBinding      = 0;
			write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.descriptorCount = 1;
			write.pBufferInfo     = &bufInfo;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		LOG_INFO(Render, "[RacePreviewViewport] InitForwardPipeline OK (forward skinné prêt)");
		return true;
	}

	void RacePreviewViewport::RenderOffscreen()
	{
		// Pré-conditions : pipeline forward prêt, matériaux câblés, mesh + pose
		// disponibles. Sinon on ne fait rien (l'image garde le clear de Init ou
		// du dernier rendu réussi).
		if (!IsForwardReady() || m_materialDescSet == VK_NULL_HANDLE
			|| m_currentMesh == nullptr
			|| m_currentMesh->vertexBuffer == VK_NULL_HANDLE
			|| m_currentMesh->indexBuffer == VK_NULL_HANDLE
			|| m_finalBoneMatrices.empty())
			return;

		// 1) Upload des matrices d'os (clamp au max alloué) + matrice d'instance.
		{
			const size_t boneCount = std::min<size_t>(m_finalBoneMatrices.size(), kMaxBones);
			const size_t boneBytes = boneCount * sizeof(engine::math::Mat4);
			if (boneBytes > 0)
			{
				void* mapped = nullptr;
				if (vkMapMemory(m_device, m_boneBufferMemory, 0, boneBytes, 0, &mapped) != VK_SUCCESS)
					return;
				std::memcpy(mapped, m_finalBoneMatrices.data(), boneBytes);
				vkUnmapMemory(m_device, m_boneBufferMemory);
			}
		}
		// Matrice modèle = importTransform (pieds à l'origine ; la caméra orbite).
		const engine::math::Mat4 modelMat = m_currentMesh->importTransform;
		{
			void* mapped = nullptr;
			if (vkMapMemory(m_device, m_instanceBufferMemory, 0, 64, 0, &mapped) != VK_SUCCESS)
				return;
			std::memcpy(mapped, modelMat.m, 64);
			vkUnmapMemory(m_device, m_instanceBufferMemory);
		}

		// 2) Caméra orbit : eye tourne autour de la cible (hauteur torse).
		const engine::math::Vec3 target(0.0f, kTargetHeightM, 0.0f);
		const engine::math::Vec3 eye(
			std::sin(m_orbitYawRad) * kOrbitRadiusM,
			kTargetHeightM,
			std::cos(m_orbitYawRad) * kOrbitRadiusM);
		const engine::math::Mat4 view = LookAtLH(eye, target);
		// Aspect = ratio d'AFFICHAGE (256x384 dans AuthImGuiCharacterCreate), pas
		// celui de l'image carrée : compense l'étirement d'ImGui::Image pour un
		// rendu non distordu (silhouette aux bonnes proportions).
		constexpr float kFovYRad = 0.785398163f; // 45°
		constexpr float kDisplayAspect = 256.0f / 384.0f;
		const engine::math::Mat4 proj = engine::math::Mat4::PerspectiveVulkan(
			kFovYRad, kDisplayAspect, 0.05f, 50.0f);
		const engine::math::Mat4 viewProj = proj * view;

		// 3) Routage peau/habit (fonction pure partagée avec le rendu in-world).
		//    Sélection genre × teinte ; repli sur la teinte claire si le matériau
		//    foncé est absent (id 0).
		uint32_t bodyMaterialId;
		if (m_gender == "female")
			bodyMaterialId = (m_skinTone == 1 && m_bodyMaterialIdFemaleDark != 0u)
				? m_bodyMaterialIdFemaleDark : m_bodyMaterialIdFemale;
		else
			bodyMaterialId = (m_skinTone == 1 && m_bodyMaterialIdMaleDark != 0u)
				? m_bodyMaterialIdMaleDark : m_bodyMaterialIdMale;
		const std::vector<uint32_t> submeshMat =
			engine::render::skinned::BuildSubmeshMaterialIndices(
				m_currentMesh->submeshes, m_bodyMaterialNames,
				bodyMaterialId, m_outfitMaterialId);

		// 4) Command buffer one-shot : begin render pass -> draws -> submit + wait.
		const bool ok = RunOneShotCommands(m_device, m_queue, m_queueFamilyIndex,
			[&](VkCommandBuffer cmd)
			{
				VkClearValue clears[2] = {};
				clears[0].color = { { 0.10f, 0.12f, 0.18f, 1.0f } };
				clears[1].depthStencil = { 1.0f, 0u };
				VkRenderPassBeginInfo rpbi = {};
				rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpbi.renderPass        = m_renderPass;
				rpbi.framebuffer       = m_framebuffer;
				rpbi.renderArea.offset = { 0, 0 };
				rpbi.renderArea.extent = { m_width, m_height };
				rpbi.clearValueCount   = 2;
				rpbi.pClearValues      = clears;
				vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

				VkViewport vpt{};
				vpt.x = 0.0f; vpt.y = 0.0f;
				vpt.width = static_cast<float>(m_width);
				vpt.height = static_cast<float>(m_height);
				vpt.minDepth = 0.0f; vpt.maxDepth = 1.0f;
				vkCmdSetViewport(cmd, 0, 1, &vpt);
				VkRect2D scissor{};
				scissor.offset = { 0, 0 };
				scissor.extent = { m_width, m_height };
				vkCmdSetScissor(cmd, 0, 1, &scissor);

				VkDescriptorSet sets[2] = { m_materialDescSet, m_boneDescSet };
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
					0, 2, sets, 0, nullptr);

				VkBuffer vbufs[2] = { m_currentMesh->vertexBuffer, m_instanceBuffer };
				VkDeviceSize offs[2] = { 0, 0 };
				vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, offs);
				vkCmdBindIndexBuffer(cmd, m_currentMesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

				struct PushConstants {
					float prevViewProj[16];
					float viewProj[16];
					uint32_t materialIndex;
					uint32_t pad0, pad1, pad2;
				} pc;
				static_assert(sizeof(PushConstants) == 144, "PushConstants must match shader layout");
				std::memcpy(pc.prevViewProj, viewProj.m, sizeof(float) * 16);
				std::memcpy(pc.viewProj,     viewProj.m, sizeof(float) * 16);
				pc.pad0 = pc.pad1 = pc.pad2 = 0;

				auto isSkin = [&](uint32_t matIdx) -> bool {
					return bodyMaterialId != 0u && matIdx == bodyMaterialId;
				};

				const bool perSubmesh = !m_currentMesh->submeshes.empty()
					&& submeshMat.size() == m_currentMesh->submeshes.size();
				if (perSubmesh)
				{
					for (size_t s = 0; s < m_currentMesh->submeshes.size(); ++s)
					{
						const engine::render::skinned::SkinnedSubMesh& sub = m_currentMesh->submeshes[s];
						if (sub.indexCount == 0) continue;
						const uint32_t matIdx = submeshMat[s];
						const bool skin = isSkin(matIdx);
						vkCmdSetDepthBias(cmd, skin ? m_skinDepthBiasConstant : 0.0f, 0.0f,
							skin ? m_skinDepthBiasSlope : 0.0f);
						pc.materialIndex = matIdx;
						vkCmdPushConstants(cmd, m_pipelineLayout,
							VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							0, sizeof(PushConstants), &pc);
						vkCmdDrawIndexed(cmd, sub.indexCount, 1, sub.firstIndex, 0, 0);
					}
				}
				else
				{
					vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
					pc.materialIndex = m_outfitMaterialId;
					vkCmdPushConstants(cmd, m_pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof(PushConstants), &pc);
					vkCmdDrawIndexed(cmd, m_currentMesh->indexCount, 1, 0, 0, 0);
				}

				vkCmdEndRenderPass(cmd);
			});
		if (!ok)
			LOG_WARN(Render, "[RacePreviewViewport] RenderOffscreen one-shot submit failed");
	}
}
