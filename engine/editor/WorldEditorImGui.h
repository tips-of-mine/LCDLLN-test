#pragma once

#include <cstdint>

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
}
namespace engine::render::terrain
{
	struct HeightmapData;
}

namespace engine::editor
{
	class WorldEditorSession;

	/// Données pour dessiner grille + aperçu brosse par-dessus la vue 3D (avant \c ImGui::Render).
	struct WorldEditorViewportOverlayDesc
	{
		const float* viewProjColMajor = nullptr;
		int viewportWidth = 0;
		int viewportHeight = 0;
		bool showGrid = false;
		float gridCellMeters = 8.f;
		float terrainOriginX = 0.f;
		float terrainOriginZ = 0.f;
		float terrainWorldSize = 1024.f;
		float heightScale = 200.f;
		const engine::render::terrain::HeightmapData* heightmap = nullptr;
		bool showBrushPreview = false;
		float brushWorldX = 0.f;
		float brushWorldZ = 0.f;
		float brushRadiusMeters = 10.f;
	};

	/// ImGui + Vulkan (rendu dynamique) pour \c lcdlln_world_editor.exe uniquement (Windows).
	/// Sur les autres plateformes, les appels sont des no-op.
	class WorldEditorImGui final
	{
	public:
		WorldEditorImGui() = default;
		WorldEditorImGui(const WorldEditorImGui&) = delete;
		WorldEditorImGui& operator=(const WorldEditorImGui&) = delete;
		WorldEditorImGui(WorldEditorImGui&&) = delete;
		WorldEditorImGui& operator=(WorldEditorImGui&&) = delete;
		~WorldEditorImGui();

		/// \param hwndNative \c HWND sous Windows, sinon ignoré.
		bool Init(VkInstance instance,
			const engine::render::VkDeviceContext& deviceContext,
			VkFormat swapchainFormat,
			uint32_t swapchainImageCount,
			uint32_t vulkanApiVersion,
			void* hwndNative);

		void Shutdown(VkDevice device);

		void OnSwapchainRecreate(uint32_t swapchainImageCount);

		/// À appeler chaque frame avant \ref RecordToBackbuffer (côté CPU).
		void NewFrame(float deltaSeconds, float displayWidth, float displayHeight);
		void BuildUi(const WorldEditorViewportOverlayDesc* viewportOverlay = nullptr);

		/// Contexte données éditeur (\c lcdlln_world_editor uniquement). Peut être nul.
		void SetEditorContext(WorldEditorSession* session, const engine::core::Config* cfg);

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
		WorldEditorSession* m_session = nullptr;
		const engine::core::Config* m_cfg = nullptr;
#if defined(_WIN32)
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
#endif
	};

} // namespace engine::editor
