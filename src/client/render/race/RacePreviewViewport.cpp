#include "src/client/render/race/RacePreviewViewport.h"

#include "src/client/render/skinned/AnimationSampler.h"
#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

	void RacePreviewViewport::Render(VkCommandBuffer cmdBuf)
	{
		// Task 11 MVP : pas de rendu mesh 3D. SkinnedRenderer::Record est
		// ecrit pour ecrire dans le framegraph principal (SceneColor_LDR +
		// GBuffer + depth), pas dans un VkImage standalone. Un refactor
		// RT-agnostic est renvoye au sous-projet C.2.
		//
		// On garde la structure Task 9 (transitions de layout +
		// vkCmdClearColorImage), mais on change la couleur du clear pour
		// donner un feedback visuel :
		//   - pas de mesh ou pas d'anim samplee : clear noir (0,0,0,1).
		//   - mesh attache + anim OK : bleu sombre (0.1, 0.1, 0.15, 1).
		// L'overlay ImGui::Text("Race : <name>") en Task 12 affichera le
		// nom de la race par-dessus pour compenser visuellement l'absence
		// de rendu 3D reel.
		//
		// Sequence : SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL,
		// vkCmdClearColorImage, TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
		// On laisse l'image en SHADER_READ_ONLY_OPTIMAL a la sortie pour
		// que ImGui::Image puisse la sampler dans la passe suivante.
		if (!IsValid()) return;

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

		// Selection conditionnelle de la couleur du clear : feedback visuel
		// "mesh attache" vs "rien a afficher". Sans rendu 3D, c'est le seul
		// indice perceptible par l'utilisateur.
		VkClearColorValue clearColor{};
		if (m_currentMesh != nullptr && !m_finalBoneMatrices.empty())
		{
			clearColor.float32[0] = 0.10f;
			clearColor.float32[1] = 0.10f;
			clearColor.float32[2] = 0.15f;
			clearColor.float32[3] = 1.00f;
		}
		else
		{
			clearColor.float32[0] = 0.0f;
			clearColor.float32[1] = 0.0f;
			clearColor.float32[2] = 0.0f;
			clearColor.float32[3] = 1.0f;
		}
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
	}
}
