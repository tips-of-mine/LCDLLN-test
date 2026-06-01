#pragma once

// src/client/render/ImpostorPass.h (M45.5 — rendu RUNTIME des impostors végétation)
//
// Passe de rendu d'impostors instanciés DANS le GBuffer deferred. Chaque
// instance = un billboard camera-facing (quad 2 triangles généré dans le VS
// depuis gl_VertexIndex) texturé depuis l'atlas octaédrique (M45.4) en fonction
// de la direction caméra→instance. Les sorties écrivent les 4 attachments
// GBuffer (albedo / normal / ORM / velocity) afin d'être éclairées par le
// LightingPass existant comme n'importe quelle géométrie opaque.
//
// Intégration : le pipeline est créé compatible avec le render pass GBuffer
// loadOp=LOAD exposé par `GeometryPass::GetRenderPassLoad()`. `RecordInstances`
// doit être appelé À L'INTÉRIEUR du callback de
// `GeometryPass::RecordTerrainChunkBatch` (render pass déjà ouvert + viewport/
// scissor configurés) — exactement comme SkyPass / SkinnedRenderer. Cette passe
// n'ouvre/ne ferme JAMAIS de render pass elle-même.
//
// Contraintes thread/timing : main thread uniquement ; `RecordInstances` entre
// vkCmdBeginRenderPass et vkCmdEndRenderPass du caller.

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// Une instance d'impostor à dessiner : centre monde + rayon de la sphère
	/// englobante du prop (demi-taille du billboard, en mètres).
	struct ImpostorInstance
	{
		float worldPos[3] = {0.0f, 0.0f, 0.0f};
		float radius      = 1.0f;
	};

	/// Push constants poussées par instance (stage VERTEX + FRAGMENT).
	/// Disposition (std430-compatible, 176 octets ≤ 256 garanti par le spec) :
	///   viewProj      : 64  (mat4 caméra)
	///   prevViewProj  : 64  (mat4 frame précédente — réservé velocity, écrit 0)
	///   cameraPos     : 16  (xyz position caméra monde, w pad)
	///   atlasParams   : 16  (x=viewsPerAxis, y=tileSize, z=fadeAlpha, w=0)
	///   instancePos   : 16  (xyz centre monde, w=radius)
	struct ImpostorPushConstants
	{
		float viewProj[16]     = {};
		float prevViewProj[16] = {};
		float cameraPos[4]     = {};
		float atlasParams[4]   = {};
		float instancePos[4]   = {};
	};
	static_assert(sizeof(ImpostorPushConstants) == 176,
		"ImpostorPushConstants doit faire 176 octets (cf. layout shader impostor.vert/.frag)");

	/// Passe de rendu d'impostors. Voir docstring fichier pour le cycle de vie.
	class ImpostorPass
	{
	public:
		ImpostorPass() = default;
		ImpostorPass(const ImpostorPass&) = delete;
		ImpostorPass& operator=(const ImpostorPass&) = delete;

		/// Crée le pipeline graphique compatible avec `gbufferLoadRenderPass`
		/// (4 color attachments + depth, depthTest ON, depthWrite ON, blend OFF
		/// sur les 4 — calque l'état MRT de GeometryPass/TerrainChunkRenderer
		/// pour ce render pass). Crée aussi le descriptor set layout (set 0 =
		/// 2 combined image samplers : albedo, normal), le pool, le set, et un
		/// sampler linéaire CLAMP_TO_EDGE partagé.
		///
		/// \param gbufferLoadRenderPass Render pass GBuffer loadOp=LOAD du caller.
		/// \param vertSpirv/vertWordCount  SPIR-V de impostor.vert.
		/// \param fragSpirv/fragWordCount  SPIR-V de impostor.frag.
		/// \return false si un objet Vulkan échoue (et l'état est nettoyé).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          VkRenderPass gbufferLoadRenderPass,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount,
		          VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Dessine `count` impostors partageant le même atlas (albedo+normal).
		/// À APPELER DANS le callback de `RecordTerrainChunkBatch` (render pass
		/// déjà ouvert). Bind pipeline + descriptor (atlas), puis pour chaque
		/// instance : push constants (avec instancePos), `vkCmdDraw(cmd,6,1,0,0)`.
		/// N'ouvre/ne ferme PAS de render pass.
		///
		/// \param extent        Étendue du viewport (informelle ; viewport/scissor
		///                      sont déjà posés par le caller via le render pass).
		/// \param instances     Tableau de `count` instances (même atlas).
		/// \param albedoView/albedoSamp  Atlas albedo (sRGB) + sampler.
		/// \param normalView/normalSamp  Atlas normal (UNORM) + sampler.
		/// \param viewProj      Matrice viewProj (16 floats, row-major `.m`).
		/// \param prevViewProj  Matrice viewProj frame précédente (16 floats).
		/// \param cameraPos3    Position caméra monde (3 floats).
		/// \param viewsPerAxis  N de la grille N×N de l'atlas.
		/// \param tileSize      Côté d'une tile en pixels.
		///
		/// Effet de bord : met à jour le descriptor set partagé (vkUpdateDescriptorSets)
		/// à chaque appel — un seul atlas peut donc être lié à la fois ; appeler
		/// une fois par atlas distinct.
		void RecordInstances(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
		          const ImpostorInstance* instances, uint32_t count,
		          VkImageView albedoView, VkSampler albedoSamp,
		          VkImageView normalView, VkSampler normalSamp,
		          const float* viewProj, const float* prevViewProj, const float* cameraPos3,
		          uint32_t viewsPerAxis, uint32_t tileSize);

		/// Sampler linéaire CLAMP_TO_EDGE créé au Init, partagé pour albedo+normal.
		/// Le caller peut le passer comme `albedoSamp`/`normalSamp` à RecordInstances.
		VkSampler GetSampler() const { return m_sampler; }

		/// Libère tous les objets Vulkan. Sûr d'appeler même non initialisé.
		void Destroy(VkDevice device);

		/// True si Init a réussi (pipeline valide).
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		/// Anneau de descriptor sets : un set est consommé par RecordInstances (un
		/// par atlas distinct). Comme les descriptors sont lus À L'EXÉCUTION du draw,
		/// réécrire un set déjà lié par un draw précédent (même cmd buffer) ou encore
		/// référencé par une frame en vol corromprait le rendu. On tourne donc sur un
		/// anneau assez grand pour couvrir (atlas distincts/frame × frames en vol).
		static constexpr uint32_t kDescRing = 64u;

		VkPipeline            m_pipeline       = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
		VkDescriptorPool      m_descPool       = VK_NULL_HANDLE;
		VkDescriptorSet       m_descSets[kDescRing] = {}; ///< Anneau (cf. kDescRing).
		uint32_t              m_descCursor     = 0;       ///< Prochain set à utiliser (mod kDescRing).
		VkSampler             m_sampler        = VK_NULL_HANDLE;
	};
}
