#pragma once

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Passe GRAPHIQUE plein écran de brouillard volumique + god rays (M45.2 — v1).
	///
	/// Version conservatrice v1 : ray-march PAR PIXEL dans un FRAGMENT shader le long
	/// du rayon caméra→pixel, en échantillonnant UNE seule cascade de shadow map
	/// (cascade 0) pour l'occlusion solaire → god rays (rayons crépusculaires).
	/// PAS de passe compute, PAS de volume froxel 3D (le frame graph ne gère ni les
	/// images 3D ni les barrières storage-image). Le multi-cascade est un suivi.
	///
	/// Structure calquée EXACTEMENT sur LightingPass : render pass 1 attachment color,
	/// descriptor set 0 de N combined image samplers, push constants fragment, pipeline
	/// fullscreen triangle (3 sommets, pas de vertex input, pas de depth test), framebuffer
	/// temporaire créé/détruit dans Record.
	///
	/// Descriptor set 0 : 3 combined image samplers
	///   binding 0 = scene color HDR post-water (sampler linéaire clamp)
	///   binding 1 = depth                       (sampler nearest clamp)
	///   binding 2 = shadow map cascade 0        (sampler nearest clamp, image DEPTH lue en .r)
	class VolumetricFogPass
	{
	public:
		/// Représentation CPU des push constants envoyées chaque frame (stage fragment).
		/// Le layout DOIT correspondre EXACTEMENT au bloc push_constant GLSL de
		/// volumetric_fog.frag (std430 / push-constant : vec4 = 16 o, mat4 = 64 o).
		struct FogParams
		{
			float invViewProj[16]; ///< Inverse view-projection, column-major (64 o).
			float sunViewProj[16]; ///< lightViewProj de la cascade 0 (matrice ombre) (64 o).
			float cameraPos[4];    ///< xyz = position caméra ; w = densité fog (<= 0 désactive) (16 o).
			float sunDir[4];       ///< xyz = direction VERS le soleil (normalisée) ; w = anisotropie HG g [-1..1] (16 o).
			float sunColor[4];     ///< xyz = couleur soleil ; w = intensité inscattering (16 o).
			float fogParams[4];    ///< x = nb de steps, y = distance max de marche (m), z = hauteur de réf (m), w = atténuation hauteur (16 o).
		};
		static_assert(sizeof(FogParams) == 192, "FogParams must be exactly 192 bytes");

		VolumetricFogPass() = default;
		VolumetricFogPass(const VolumetricFogPass&) = delete;
		VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

		/// Crée la render pass, le descriptor set layout, le descriptor pool, les samplers
		/// et le pipeline graphique plein écran.
		/// \param sceneColorHDRFormat  Format de l'image de sortie (doit être VK_FORMAT_R16G16B16A16_SFLOAT).
		/// \param vertSpirv / vertWordCount  SPIR-V du vertex shader (fullscreen triangle réutilisé).
		/// \param fragSpirv / fragWordCount  SPIR-V du fragment shader (volumetric_fog.frag).
		/// \param maxFrames  Nombre de frames en vol ; un descriptor set est alloué par frame.
		/// \return false si un objet Vulkan échoue (l'appelant ne doit pas faire échouer le boot).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe brouillard volumique dans cmd.
		/// Met à jour le descriptor set de la frame avec les 3 vues (scene color, depth,
		/// shadow cascade 0), crée un framebuffer temporaire sur idSceneColorOut, ouvre la
		/// render pass, push les FogParams, dessine le triangle plein écran, ferme la render
		/// pass et détruit le framebuffer immédiatement.
		/// \param idSceneColorIn    SceneColor HDR post-water (lu en entrée).
		/// \param idDepth           Depth scene (D32_SFLOAT).
		/// \param idShadowCascade0  Shadow map cascade 0 (image DEPTH).
		/// \param idSceneColorOut   Cible de sortie (SceneColor HDR brouillardée).
		/// \param frameIndex        Index de frame en vol (0 .. maxFrames-1).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idSceneColorIn, ResourceId idDepth, ResourceId idShadowCascade0,
			ResourceId idSceneColorOut, const FogParams& params, uint32_t frameIndex);

		/// Libère toutes les ressources Vulkan. Sûr à appeler même si non initialisé.
		void Destroy(VkDevice device);

		/// Renvoie true si le pipeline et la render pass sont valides.
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE; ///< Linéaire clamp, pour la scene color HDR.
		VkSampler             m_nearestSampler      = VK_NULL_HANDLE; ///< Nearest clamp, pour depth + shadow map.

		std::vector<VkDescriptorSet> m_descriptorSets; ///< Un par frame en vol.
		uint32_t m_maxFrames = 2;
	};

} // namespace engine::render
