#pragma once

#include <cstdint>
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

		/// Render le mesh courant dans l'offscreen RT. Doit etre appele
		/// dans un command buffer en cours d'enregistrement, AVANT
		/// l'appel ImGui_ImplVulkan_RenderDrawData qui consomme la
		/// texture. Si SetMesh n'a jamais ete appele ou mesh = nullptr,
		/// Render se contente d'un clear color (image noire).
		/// Task 9 skeleton : juste un vkCmdClearColorImage(noir) +
		/// transitions de layout SHADER_READ_ONLY ↔ TRANSFER_DST.
		/// Task 11 : ajoute le rendu skinned + mini-camera orbit.
		///
		/// Effet de bord : modifie le layout de m_image pendant l'appel
		/// (laisse SHADER_READ_ONLY_OPTIMAL a la sortie).
		void Render(VkCommandBuffer cmdBuf);

		bool        IsValid()           const { return m_image != VK_NULL_HANDLE; }
		VkImage     GetImage()          const { return m_image; }
		VkImageView GetImageView()      const { return m_view; }

		/// Descriptor ImGui (cast de VkDescriptorSet) pour ImGui::Image.
		/// Retourne 0 si IsValid() == false.
		uint64_t    GetImguiTextureId() const { return m_imguiTextureId; }

	private:
		/// Format fixe : R8G8B8A8 UNORM. Alignement standard
		/// ImGui_ImplVulkan, identique a EditorViewportRenderTarget.
		static constexpr VkFormat kFormat         = VK_FORMAT_R8G8B8A8_UNORM;

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
	};
}
