#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

struct ImDrawList;

namespace engine::editor::world
{
	/// Image VkImage R8G8B8A8 dédiée à recevoir le rendu offscreen du
	/// viewport éditeur, exposée à ImGui pour affichage dans le `ScenePanel`
	/// via `ImGui::Image(GetImguiTextureId(), ...)` (M100.34 — préalable au
	/// multi-viewport / vue 4-écrans).
	///
	/// PR 1 (cette livraison) : infrastructure seule — l'image est créée,
	/// transitionnée en `SHADER_READ_ONLY_OPTIMAL`, exposée à ImGui, mais
	/// **aucun contenu** n'est encore écrit dedans (rendu offscreen branché
	/// dans la PR suivante via une passe FrameGraph qui copie
	/// `SceneColor_LDR` vers cette image).
	///
	/// Cycle de vie typique :
	///   1. `Init(device, physDev, queue, queueFamilyIndex, width, height)` au boot.
	///   2. `Resize(w, h)` quand le ScenePanel ou la swapchain changent de taille.
	///   3. `GetImguiTextureId()` consommé par `ScenePanel::Render` chaque frame.
	///   4. `Shutdown(device)` au shutdown Engine.
	///
	/// Contraintes thread/timing : main thread Vulkan. Toutes les opérations
	/// Vulkan (create/destroy image, allocate memory, register descriptor ImGui)
	/// sont synchrones.
	class EditorViewportRenderTarget
	{
	public:
		EditorViewportRenderTarget() = default;
		~EditorViewportRenderTarget();

		EditorViewportRenderTarget(const EditorViewportRenderTarget&)            = delete;
		EditorViewportRenderTarget& operator=(const EditorViewportRenderTarget&) = delete;

		/// Alloue VkImage + memory + view + sampler + descriptor ImGui.
		/// Transition de l'image en `SHADER_READ_ONLY_OPTIMAL` immédiatement
		/// pour que `ImGui::Image` puisse l'échantillonner même si rien n'a
		/// encore été copié dedans (l'image est noire au démarrage —
		/// défaut du `VK_IMAGE_LAYOUT_UNDEFINED → SHADER_READ_ONLY`).
		///
		/// \return false si une des allocations échoue. Dans ce cas l'objet
		/// est laissé en état "non initialisé" (`IsValid() == false`).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue queue, uint32_t queueFamilyIndex,
			uint32_t initialWidth, uint32_t initialHeight);

		/// Détruit toutes les ressources Vulkan + le descriptor ImGui.
		/// Idempotent — appelable plusieurs fois. Le device doit être idle
		/// (cf. `vkDeviceWaitIdle`).
		void Shutdown(VkDevice device);

		/// Re-crée l'image à la taille `(w, h)`. No-op si la taille est
		/// inchangée. À appeler depuis le main thread après que le device
		/// soit idle (cf. swapchain recreate path dans Engine.cpp). Le
		/// descriptor ImGui est recréé — l'ancien ID devient invalide.
		bool Resize(VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue queue, uint32_t queueFamilyIndex,
			uint32_t newWidth, uint32_t newHeight);

		bool     IsValid()           const { return m_image != VK_NULL_HANDLE; }
		uint32_t GetWidth()          const { return m_width; }
		uint32_t GetHeight()         const { return m_height; }
		VkImage  GetImage()          const { return m_image; }
		VkImageView GetImageView()   const { return m_view; }
		VkFormat GetFormat()         const { return kFormat; }

		/// Descriptor ImGui à passer à `ImGui::Image`. Retourne 0 si
		/// `IsValid() == false`. Le descriptor est stable tant que `Resize`
		/// ou `Shutdown` n'est pas appelé.
		uint64_t GetImguiTextureId() const;

	private:
		/// Format fixe : R8G8B8A8 UNORM, alignement standard ImGui_ImplVulkan.
		/// Le rendu source (SceneColor_LDR du FrameGraph) est en format
		/// HDR ; la PR 2 ajoutera une passe tonemap-aware lors de la copie
		/// si nécessaire (vkCmdBlitImage ou conversion explicite).
		static constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

		/// Helper interne : détruit toutes les ressources sauf le sampler
		/// (réutilisable entre Resize). Idempotent.
		void DestroyImageResources(VkDevice device);

		/// Crée image + memory + view + descriptor ImGui à la taille (w, h).
		/// Transitionne l'image neuve en `SHADER_READ_ONLY_OPTIMAL`.
		bool CreateImageResources(VkDevice device, VkPhysicalDevice physicalDevice,
			VkQueue queue, uint32_t queueFamilyIndex,
			uint32_t w, uint32_t h);

		VkImage        m_image     = VK_NULL_HANDLE;
		VkImageView    m_view      = VK_NULL_HANDLE;
		VkDeviceMemory m_memory    = VK_NULL_HANDLE;
		VkSampler      m_sampler   = VK_NULL_HANDLE;
		uint32_t       m_width     = 0;
		uint32_t       m_height    = 0;

		/// `VkDescriptorSet` retournée par `ImGui_ImplVulkan_AddTexture`.
		/// Opaque depuis l'extérieur de ce header (ImGui types non
		/// exposés). Stocké en `uint64_t` pour éviter d'inclure imgui
		/// dans le header.
		uint64_t       m_imguiTextureId = 0u;
	};
}
