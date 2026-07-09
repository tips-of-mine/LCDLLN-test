#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::render::skinned
{
	struct SkinnedMesh;
}

namespace engine::render::race
{
	/// Sous-projet C MVP — Viewport offscreen Vulkan dedie a l'apercu
	/// 3D du mesh skinned d'une race dans l'ecran de creation de
	/// personnage (AuthImGuiCharacterCreate).
	///
	/// Architecture (copiee depuis EditorViewportRenderTarget M100.34) :
	/// - VkImage + VkImageView + VkSampler offscreen 512x512 RGBA8.
	/// - Descriptor enregistre dans ImGui via ImGui_ImplVulkan_AddTexture
	///   -> handle stocke en uint64_t et passe a ImGui::Image dans
	///      AuthImGuiCharacterCreate.
	/// - Init transitionne l'image en SHADER_READ_ONLY_OPTIMAL (image
	///   noire au demarrage, affichable meme avant le 1er Render).
	///
	/// Render (Task 11) : rendu skinned du mesh courant avec mini-
	/// camera orbit. Tick (Task 10) avance l'angle de rotation et
	/// echantillonne le clip Idle.
	///
	/// Cycle de vie typique :
	///   1. Init au boot Engine (apres ImGui_ImplVulkan_Init).
	///   2. SetMesh quand l'utilisateur change de race dans le combo.
	///   3. Tick + Render chaque frame de l'ecran de creation.
	///   4. Shutdown au shutdown Engine (avant device idle).
	class RacePreviewViewport
	{
	public:
		RacePreviewViewport() = default;
		~RacePreviewViewport();

		RacePreviewViewport(const RacePreviewViewport&)            = delete;
		RacePreviewViewport& operator=(const RacePreviewViewport&) = delete;

		/// Alloue VkImage + memory + view + sampler + descriptor ImGui.
		/// Transitionne l'image en SHADER_READ_ONLY_OPTIMAL pour que
		/// ImGui::Image puisse l'echantillonner avant le 1er Render.
		///
		/// \param device Vulkan logical device — doit etre valide.
		/// \param physicalDevice utilise pour query les memory types.
		/// \param queue queue graphique pour le command buffer one-shot
		///        de transition de layout.
		/// \param queueFamilyIndex famille de la queue (pour le pool).
		/// \param width largeur de l'image offscreen (defaut 512).
		/// \param height hauteur de l'image offscreen (defaut 512).
		/// \return false si une allocation echoue. Dans ce cas l'objet
		/// reste en etat "non valide" (IsValid() == false).
		///
		/// Effets de bord : enregistre un descriptor ImGui via
		/// ImGui_ImplVulkan_AddTexture ; doit etre appele apres
		/// ImGui_ImplVulkan_Init en main thread.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          VkQueue queue, uint32_t queueFamilyIndex,
		          uint32_t width = 512, uint32_t height = 512);

		/// Detruit toutes les ressources Vulkan + le descriptor ImGui.
		/// Idempotent. Le device doit etre idle (vkDeviceWaitIdle) avant
		/// l'appel.
		void Shutdown(VkDevice device);

		/// Change le mesh affiche. Appele depuis AuthImGuiCharacterCreate
		/// chaque fois que l'utilisateur selectionne une race differente.
		/// Pointer non-owning : le mesh appartient a Engine::m_raceMeshes.
		/// \param mesh nullptr pour "pas de mesh" (Render fera juste un
		///        clear noir).
		void SetMesh(engine::render::skinned::SkinnedMesh* mesh);

		/// Avance l'etat d'animation : rotation orbit (30 deg/sec) +
		/// sampling du clip Idle. Doit etre appele chaque frame de
		/// l'ecran de creation. Skeleton vide en Task 9 (rempli en Task 10).
		/// \param dt delta time en secondes depuis la frame precedente.
		void Tick(float dt);

		/// Phase 2 — Initialise le pipeline forward skinné (render pass color+depth,
		/// pipeline, bone SSBO set 1, instance buffer) en plus de l'image créée par
		/// Init. À appeler UNE fois après Init, quand le descriptor set layout
		/// matériau (set 0) et le SPIR-V des shaders d'aperçu sont disponibles.
		///
		/// \param materialLayout Descriptor set layout matériau (set 0, bindless) —
		///        identique à celui de GeometryPass/SkinnedRenderer.
		/// \param vertSpirv  SPIR-V de skinned_preview.vert.spv.
		/// \param vertWordCount Nombre de uint32_t dans vertSpirv.
		/// \param fragSpirv  SPIR-V de skinned_preview.frag.spv.
		/// \param fragWordCount Nombre de uint32_t dans fragSpirv.
		/// \return false si une étape Vulkan échoue (IsForwardReady() reste false ;
		///         RenderOffscreen retombe alors sur le clear de Init).
		///
		/// Effet de bord : alloue 1 VkRenderPass + 1 VkFramebuffer + 1
		/// VkPipelineLayout + 1 VkPipeline + 1 VkDescriptorSetLayout/Pool/Set bone
		/// + 2 VkBuffer (bone SSBO + instance). Doit être appelé en main thread,
		/// device valide (stocké par Init).
		bool InitForwardPipeline(VkDescriptorSetLayout materialLayout,
		                         const uint32_t* vertSpirv, size_t vertWordCount,
		                         const uint32_t* fragSpirv, size_t fragWordCount);

		/// Genre actif de l'aperçu ("male"/"female") : route la peau au prochain
		/// RenderOffscreen. Toute valeur != "female" est traitée comme "male".
		void SetGender(const std::string& gender);

		/// Teinte de peau active : 0 = claire (défaut), 1 = foncée. Sélectionne
		/// le matériau peau clair/foncé au prochain RenderOffscreen. Repli sur la
		/// teinte claire si le matériau foncé n'existe pas (id 0).
		void SetSkinTone(int tone);

		/// Active/désactive la rotation orbit automatique (défaut true). Off => la
		/// caméra reste à l'angle courant (piloté par SetOrbitYaw, ex. drag souris
		/// dans la fenêtre Personnage). L'animation continue indépendamment.
		void SetAutoOrbit(bool a) { m_autoOrbit = a; }

		/// Fixe l'angle orbit (radians). 0 = vue de face (caméra en +Z regardant le
		/// perso). Utilisé quand l'auto-orbit est off (rotation manuelle).
		void SetOrbitYaw(float rad) { m_orbitYawRad = rad; }

		/// Matériaux de l'avatar (set bindless + ids + noms peau + depth bias),
		/// fournis par Engine après création des matériaux au boot. À appeler une
		/// fois avant le 1er RenderOffscreen (re-appelable sans risque).
		/// \param materialSet Descriptor set bindless (set 0) partagé de l'engine.
		/// \param outfitId    Index matériau habit (défaut des sous-maillages).
		/// \param bodyMaleId  Index matériau peau masculine (0 = aucun).
		/// \param bodyFemaleId Index matériau peau féminine (0 = aucun).
		/// \param bodyNames   Noms de matériaux glTF considérés comme peau.
		/// \param skinDepthBiasConstant Depth bias constant peau (anti z-fight).
		/// \param skinDepthBiasSlope    Depth bias slope peau.
		void SetAvatarMaterials(VkDescriptorSet materialSet, uint32_t outfitId,
		                        uint32_t bodyMaleId, uint32_t bodyFemaleId,
		                        uint32_t bodyMaleDarkId, uint32_t bodyFemaleDarkId,
		                        const std::vector<std::string>& bodyNames,
		                        float skinDepthBiasConstant, float skinDepthBiasSlope);

		/// Phase 2 — Rend le mesh courant en 3D dans l'image offscreen via un
		/// command buffer one-shot autonome (begin/submit/wait idle), de façon à
		/// laisser l'image en SHADER_READ_ONLY_OPTIMAL prête pour ImGui::Image.
		/// À appeler chaque frame de l'écran de création, AVANT que la draw list
		/// ImGui n'échantillonne la texture. Ne fait rien (retour immédiat) si
		/// le pipeline forward n'est pas prêt, ou si aucun mesh/aucune pose n'est
		/// disponible (l'image conserve alors le clear de Init / du dernier rendu).
		///
		/// Effet de bord : soumet du travail sur la queue graphique + vkQueueWaitIdle
		/// (stall court, acceptable sur l'écran de création non perf-critique).
		void RenderOffscreen();

		bool        IsValid()           const { return m_image != VK_NULL_HANDLE; }
		bool        IsForwardReady()    const { return m_pipeline != VK_NULL_HANDLE; }
		VkImage     GetImage()          const { return m_image; }
		VkImageView GetImageView()      const { return m_view; }

		/// Descriptor ImGui (cast de VkDescriptorSet) pour ImGui::Image.
		/// Retourne 0 si IsValid() == false.
		uint64_t    GetImguiTextureId() const { return m_imguiTextureId; }

	private:
		/// Format fixe : R8G8B8A8 UNORM. Alignement standard
		/// ImGui_ImplVulkan, identique a EditorViewportRenderTarget.
		static constexpr VkFormat kFormat         = VK_FORMAT_R8G8B8A8_UNORM;

		/// Format du depth buffer de l'aperçu (phase 2).
		static constexpr VkFormat kDepthFormat    = VK_FORMAT_D32_SFLOAT;

		/// Nombre maximal de bones supporté (dimensionne le bone SSBO). Aligné
		/// sur SkinnedRenderer (256, suffisant pour Mixamo / UE5).
		static constexpr uint32_t kMaxBones       = 256u;

		/// Vitesse de rotation de la camera orbit autour du mesh, en
		/// degres par seconde (utilise par Tick en Task 10).
		static constexpr float    kOrbitDegPerSec = 30.0f;

		/// Rayon de la camera orbit en metres (utilise par Render en
		/// Task 11).
		static constexpr float    kOrbitRadiusM   = 2.5f;

		/// Hauteur du centre du mesh en metres (point fixe vise par la
		/// camera orbit). Approxime ~ hauteur torse d'un humanoid.
		static constexpr float    kTargetHeightM  = 0.9f;

		VkImage        m_image          = VK_NULL_HANDLE;
		VkImageView    m_view           = VK_NULL_HANDLE;
		VkDeviceMemory m_memory         = VK_NULL_HANDLE;
		VkSampler      m_sampler        = VK_NULL_HANDLE;
		uint32_t       m_width          = 0;
		uint32_t       m_height         = 0;

		// --- Phase 2 : contexte Vulkan mémorisé par Init pour le rendu autonome ---
		VkDevice         m_device           = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice   = VK_NULL_HANDLE;
		VkQueue          m_queue            = VK_NULL_HANDLE;
		uint32_t         m_queueFamilyIndex = 0u;

		// --- Phase 2 : depth buffer de l'aperçu ---
		VkImage        m_depthImage       = VK_NULL_HANDLE;
		VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
		VkImageView    m_depthImageView   = VK_NULL_HANDLE;

		// --- Phase 2 : pipeline forward skinné ---
		VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
		VkFramebuffer    m_framebuffer    = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline       m_pipeline       = VK_NULL_HANDLE;

		// --- Phase 2 : bone SSBO (set 1) — slot unique (RenderOffscreen attend la
		// fin du GPU à chaque frame, donc pas de course FIF à gérer ici) ---
		VkBuffer              m_boneBuffer       = VK_NULL_HANDLE;
		VkDeviceMemory        m_boneBufferMemory = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_boneSetLayout    = VK_NULL_HANDLE;
		VkDescriptorPool      m_boneDescPool     = VK_NULL_HANDLE;
		VkDescriptorSet       m_boneDescSet      = VK_NULL_HANDLE;

		// --- Phase 2 : instance buffer (1 mat4 modèle = cadrage avatar) ---
		VkBuffer       m_instanceBuffer       = VK_NULL_HANDLE;
		VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

		// --- Phase 2 : matériaux avatar (set 0 bindless fourni par Engine) ---
		VkDescriptorSet          m_materialDescSet      = VK_NULL_HANDLE;
		uint32_t                 m_outfitMaterialId         = 0u;
		uint32_t                 m_bodyMaterialIdMale       = 0u;
		uint32_t                 m_bodyMaterialIdFemale     = 0u;
		uint32_t                 m_bodyMaterialIdMaleDark   = 0u;
		uint32_t                 m_bodyMaterialIdFemaleDark = 0u;
		std::vector<std::string> m_bodyMaterialNames;
		std::string              m_gender               = "male";
		int                      m_skinTone             = 0; ///< 0 = claire, 1 = foncée.
		float                    m_skinDepthBiasConstant = 0.0f;
		float                    m_skinDepthBiasSlope    = 0.0f;

		/// `VkDescriptorSet` retournee par `ImGui_ImplVulkan_AddTexture`.
		/// Stockee en uint64_t pour eviter d'inclure imgui dans le header
		/// (memes raisons que EditorViewportRenderTarget).
		uint64_t       m_imguiTextureId = 0u;

		/// Mesh affiche dans le preview. Non-owning : appartient a
		/// Engine::m_raceMeshes. Peut etre nullptr (Render fait alors
		/// juste un clear noir).
		engine::render::skinned::SkinnedMesh* m_currentMesh = nullptr;

		/// Angle de rotation orbit accumule depuis Init, en radians.
		/// Wrap mod 2pi dans Tick pour eviter l'accumulation flottante.
		float          m_orbitYawRad    = 0.0f;

		/// Rotation automatique de l'orbit dans Tick (défaut true, ex. écran de
		/// création). La fenêtre Personnage la coupe pour une rotation manuelle.
		bool           m_autoOrbit      = true;

		/// Anim sampling state mis a jour par Tick, consomme par Render.
		/// m_localBoneMatrices : matrices TRS locales echantillonnees du clip Idle.
		/// m_globalBoneMatrices : matrices globales (parent * local) propagees par
		///   la hierarchie du squelette.
		/// m_finalBoneMatrices : matrices finales (global * inverseBindGlobal) pretes
		///   a etre uploadees a un shader de skinning. Vides si pas de mesh / clip.
		std::vector<engine::math::Mat4> m_localBoneMatrices;
		std::vector<engine::math::Mat4> m_globalBoneMatrices;
		std::vector<engine::math::Mat4> m_finalBoneMatrices;

		/// Timestamp (steady_clock seconds) du 1er Tick ou un mesh etait
		/// disponible. Sert d'origine au calcul de l'instant d'echantillonnage
		/// `elapsed = nowSec - m_sampleStartSec`. Initialise paresseusement
		/// dans Tick pour eviter qu'un long delai entre Init et l'apparition
		/// du mesh fasse "sauter" l'anim au milieu du clip.
		float          m_sampleStartSec = 0.0f;
	};
}
