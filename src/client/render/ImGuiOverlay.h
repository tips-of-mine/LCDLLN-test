#pragma once

#include <cstdint>
#include <functional>

#include <vulkan/vulkan_core.h>

namespace engine::core
{
	class Config;
}
namespace engine::platform
{
	class Window;
}

namespace engine::render
{
	class VkDeviceContext;

	/// Overlay ImGui commun (Windows : backends Win32 + Vulkan en rendu dynamique).
	/// Possède le contexte ImGui, le pool de descripteurs et l'atlas de polices ;
	/// dessine la frame ImGui par-dessus le backbuffer. Utilisé par les fenêtres
	/// de jeu (chat, HUD, guilde, hôtel des ventes…) et par l'éditeur monde, qui
	/// y ajoute ses panneaux (WorldEditorImGui::BuildUi).
	/// Sur les autres plateformes, les appels sont des no-op (IsReady() == false).
	class ImGuiOverlay final
	{
	public:
		ImGuiOverlay();
		ImGuiOverlay(const ImGuiOverlay&) = delete;
		ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;
		ImGuiOverlay(ImGuiOverlay&&) = delete;
		ImGuiOverlay& operator=(ImGuiOverlay&&) = delete;
		~ImGuiOverlay();

		/// Crée le contexte ImGui, charge les polices et initialise les backends.
		/// \param hwndNative \c HWND sous Windows, sinon ignoré.
		/// \param cfg utilisé pour charger les polices TTF de l'UI auth (Windlass / Morpheus) dans
		/// l'atlas ImGui avant la création de la texture de fonts par ImGui_ImplVulkan_Init.
		/// \param isWorldEditorExe \c true pour \c lcdlln_world_editor.exe : la police par defaut
		/// devient Arial (lisible, neutre pour un editeur de carte) au lieu de Windlass (decorative,
		/// reservee a l'UI auth/in-game). Cf. \c editor.font.arial_path / \c editor.font.arial_pixel_height
		/// dans config.json. Aucun effet si \c false (UI auth garde Windlass).
		/// \param styleHook appelé (si \p isWorldEditorExe) juste après le style sombre par
		/// défaut, avant le chargement des polices : l'éditeur y applique son thème.
		/// \return true si l'overlay est prêt. Main thread uniquement.
		bool Init(::VkInstance instance,
			const engine::render::VkDeviceContext& deviceContext,
			VkFormat swapchainFormat,
			uint32_t swapchainImageCount,
			uint32_t vulkanApiVersion,
			void* hwndNative,
			const engine::core::Config* cfg = nullptr,
			bool isWorldEditorExe = false,
			const std::function<void()>& styleHook = {});

		/// Libère backends, contexte ImGui et pool de descripteurs (vkDeviceWaitIdle).
		void Shutdown(VkDevice device);

		/// À appeler après une recréation de swapchain (nombre d'images minimal).
		void OnSwapchainRecreate(uint32_t swapchainImageCount);

		/// À appeler chaque frame avant \ref RecordToBackbuffer (côté CPU) : ImGui::NewFrame.
		void NewFrame(float deltaSeconds, float displayWidth, float displayHeight);

		/// Win32 : branche \c ImGui_ImplWin32_WndProcHandler avant le traitement LCDLLN.
		void AttachPlatformWindow(void* hwndNative, engine::platform::Window& window);
		void DetachPlatformWindow(engine::platform::Window& window);

		[[nodiscard]] bool IsReady() const { return m_ready; }
		[[nodiscard]] bool WantsCaptureMouse() const;
		[[nodiscard]] bool WantsCaptureKeyboard() const;

		/// Image swapchain en \c TRANSFER_DST_OPTIMAL → présentation. Dessine ImGui par-dessus la scène copiée.
		bool RecordToBackbuffer(VkCommandBuffer cmd,
			VkImage backbufferImage,
			VkImageView backbufferView,
			VkExtent2D extent,
			const engine::render::VkDeviceContext& deviceContext);

	private:
		bool m_ready = false;
		void* m_hwnd = nullptr;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	};
} // namespace engine::render
