#pragma once

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Passe GRAPHIQUE plein écran de profondeur de champ / bokeh (M45.3 — v1).
	///
	/// S'exécute sur l'image HDR APRÈS bloom et AVANT tonemap (sur HDR, pour des
	/// highlights bokeh propres). Pour CHAQUE pixel on calcule un Circle of Confusion
	/// (CoC) depuis la distance monde reconstruite (depth + invViewProj), puis on fait
	/// un flou en disque pondéré par le CoC (16 samples fixes). Un sample n'est mélangé
	/// que si son PROPRE CoC le place dans le flou (anti-bleeding à travers une arête
	/// nette de premier plan). PAS de passe compute, PAS de pré-pass CoC séparée
	/// (v1 conservatrice, tout dans un seul fragment shader).
	///
	/// Structure calquée EXACTEMENT sur VolumetricFogPass : render pass 1 attachment
	/// color (load DONT_CARE, on réécrit tout le plein écran), descriptor set 0 de N
	/// combined image samplers, push constants fragment, pipeline fullscreen triangle
	/// (3 sommets, pas de vertex input, pas de depth test), framebuffer mis en cache
	/// dans Record (pattern WaterPass).
	///
	/// Descriptor set 0 : 2 combined image samplers
	///   binding 0 = scene color HDR post-bloom (sampler LINÉAIRE clamp)
	///   binding 1 = depth                       (sampler NEAREST clamp)
	class DepthOfFieldPass
	{
	public:
		/// Représentation CPU des push constants envoyées chaque frame (stage fragment).
		/// Le layout DOIT correspondre EXACTEMENT au bloc push_constant GLSL de
		/// dof.frag (std430 / push-constant : vec4 = 16 o, mat4 = 64 o).
		struct DofParams
		{
			float invViewProj[16]; ///< Inverse view-projection, column-major (64 o) — reconstruction world pos depuis depth.
			float cameraPos[4];    ///< xyz = position caméra ; w = distance focale (m) (16 o).
			float dofParams[4];    ///< x = plage de netteté (m, demi-largeur ; <=0 désactive la passe = passthrough), y = rayon de flou max (px), z = échelle flou near, w = échelle flou far (16 o).
			float texelSize[4];    ///< x = 1/width, y = 1/height, z/w = 0 (padding) (16 o).
		};
		static_assert(sizeof(DofParams) == 112, "DofParams must be exactly 112 bytes");

		DepthOfFieldPass() = default;
		DepthOfFieldPass(const DepthOfFieldPass&) = delete;
		DepthOfFieldPass& operator=(const DepthOfFieldPass&) = delete;

		/// Crée la render pass, le descriptor set layout, le descriptor pool, les samplers
		/// et le pipeline graphique plein écran.
		/// \param sceneColorHDRFormat  Format de l'image de sortie (doit être VK_FORMAT_R16G16B16A16_SFLOAT).
		/// \param vertSpirv / vertWordCount  SPIR-V du vertex shader (fullscreen triangle réutilisé).
		/// \param fragSpirv / fragWordCount  SPIR-V du fragment shader (dof.frag).
		/// \param maxFrames  Nombre de frames en vol ; un descriptor set est alloué par frame.
		/// \return false si un objet Vulkan échoue (l'appelant ne doit pas faire échouer le boot).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe profondeur de champ dans cmd.
		/// Met à jour le descriptor set de la frame avec les 2 vues (scene color, depth),
		/// récupère (ou crée) le framebuffer dans le cache interne, ouvre la render pass,
		/// push les DofParams, dessine le triangle plein écran et ferme la render pass.
		/// \param idColorIn   SceneColor HDR post-bloom (lu en entrée, sampler linéaire).
		/// \param idDepth     Depth scene (D32_SFLOAT, sampler nearest).
		/// \param idColorOut  Cible de sortie (SceneColor HDR floutée).
		/// \param frameIndex  Index de frame en vol (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idColorIn, ResourceId idDepth, ResourceId idColorOut,
			const DofParams& params, uint32_t frameIndex);

		/// Libère toutes les ressources Vulkan. Sûr à appeler même si non initialisé.
		void Destroy(VkDevice device);

		/// Détruit les framebuffers cachés (appeler au resize avant FG destroy).
		void InvalidateFramebufferCache(VkDevice device);

		/// Renvoie true si le pipeline et la render pass sont valides.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		// Audit 2026-06-10 (Lot B2) — cache framebuffer (pattern WaterPass) :
		// l'ancien framebuffer temporaire était détruit avant le vkQueueSubmit (UB).
		struct FramebufferKey
		{
			VkImageView outputView = VK_NULL_HANDLE;
			uint32_t width = 0;
			uint32_t height = 0;
			bool operator==(const FramebufferKey& o) const noexcept
			{
				return outputView == o.outputView && width == o.width && height == o.height;
			}
		};
		struct FramebufferKeyHash
		{
			size_t operator()(const FramebufferKey& k) const noexcept
			{
				const size_t hView = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.outputView));
				const size_t hW = std::hash<uint32_t>{}(k.width);
				const size_t hH = std::hash<uint32_t>{}(k.height);
				return hView ^ (hW + 0x9e3779b9u) ^ (hH + 0x85ebca6bu);
			}
		};
		std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_framebufferCache;

		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE; ///< Linéaire clamp, pour la scene color HDR.
		VkSampler             m_nearestSampler      = VK_NULL_HANDLE; ///< Nearest clamp, pour depth.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< Un par frame en vol.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
