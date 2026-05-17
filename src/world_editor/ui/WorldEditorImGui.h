#pragma once

#include <cstdint>
#include <memory>

#include <vector>

#include <vulkan/vulkan_core.h>

#include "src/world_editor/ui/WorldMapEditDocument.h"

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
	class DayNightCycle;
}
namespace engine::render::terrain
{
	struct HeightmapData;
}
namespace engine::editor::world
{
	class WorldEditorShell;
}
namespace engine::editor::world::zone_presets
{
	class ZonePresetDialog;
}

namespace engine::editor
{
	class WorldEditorSession;
	class TexturePreviewCache;  // vignettes splatting (Task 12)

	/// Données pour dessiner grille + aperçu brosse par-dessus la vue 3D (avant \c ImGui::Render).
	struct WorldEditorViewportOverlayDesc
	{
		const float* viewProjColMajor = nullptr;
		/// Position / angles caméra (monde), remplis chaque frame — la grille utilise \c viewProjColMajor, pas ces champs.
		float cameraWorldX = 0.f;
		float cameraWorldY = 0.f;
		float cameraWorldZ = 0.f;
		float cameraYawDeg = 0.f;
		float cameraPitchDeg = 0.f;
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
		/// Marqueurs debug instances (monde m). Nullptr = rien à dessiner.
		const std::vector<WorldMapEditLayoutInstance>* layoutInstancesOverlay = nullptr;
		int selectedLayoutInstanceOverlay = -1;
	};

	/// ImGui + Vulkan (rendu dynamique) pour \c lcdlln_world_editor.exe uniquement (Windows).
	/// Sur les autres plateformes, les appels sont des no-op.
	class WorldEditorImGui final
	{
	public:
		/// Constructeur out-of-line : la définition vit dans le `.cpp` où
		/// `ZonePresetDialog.h` est inclus en entier. Sans cela, le
		/// unique_ptr<ZonePresetDialog> ne peut pas instancier son cleanup
		/// d'exception (type incomplet vu depuis Engine.cpp).
		WorldEditorImGui();
		WorldEditorImGui(const WorldEditorImGui&) = delete;
		WorldEditorImGui& operator=(const WorldEditorImGui&) = delete;
		WorldEditorImGui(WorldEditorImGui&&) = delete;
		WorldEditorImGui& operator=(WorldEditorImGui&&) = delete;
		~WorldEditorImGui();

		/// Branche le `WorldEditorShell` propriétaire des 4 documents
		/// (terrain / water / mesh inserts / dungeon portals), du
		/// `CommandStack` et des 4 catalogs (caves, overhangs, arches,
		/// dungeons) — requis pour l'entrée menu Fichier > « Appliquer un
		/// preset de zone » (M100.46 incrément 3). Pointeur non possédé.
		/// Si nul, l'entrée menu est désactivée.
		void SetWorldEditorShell(engine::editor::world::WorldEditorShell* shell)
		{
			m_shell = shell;
		}

		/// \param hwndNative \c HWND sous Windows, sinon ignoré.
		/// \param cfg utilisé pour charger les polices TTF de l'UI auth (Windlass / Morpheus) dans
		/// l'atlas ImGui avant la création de la texture de fonts par ImGui_ImplVulkan_Init.
		/// \param isWorldEditorExe \c true pour \c lcdlln_world_editor.exe : la police par defaut
		/// devient Arial (lisible, neutre pour un editeur de carte) au lieu de Windlass (decorative,
		/// reservee a l'UI auth/in-game). Cf. \c editor.font.arial_path / \c editor.font.arial_pixel_height
		/// dans config.json. Aucun effet si \c false (UI auth garde Windlass).
		bool Init(VkInstance instance,
			const engine::render::VkDeviceContext& deviceContext,
			VkFormat swapchainFormat,
			uint32_t swapchainImageCount,
			uint32_t vulkanApiVersion,
			void* hwndNative,
			const engine::core::Config* cfg = nullptr,
			bool isWorldEditorExe = false);

		void Shutdown(VkDevice device);

		void OnSwapchainRecreate(uint32_t swapchainImageCount);

		/// À appeler chaque frame avant \ref RecordToBackbuffer (côté CPU).
		void NewFrame(float deltaSeconds, float displayWidth, float displayHeight);
		void BuildUi(const WorldEditorViewportOverlayDesc* viewportOverlay = nullptr);

		/// Contexte données éditeur (\c lcdlln_world_editor uniquement). Peut être nul.
		void SetEditorContext(WorldEditorSession* session, engine::core::Config* cfg);

		/// Branche le DayNightCycle pour que le panneau "Atmosphere" puisse modifier
		/// la time-of-day, le timeScale et l'ambient en live. Pointeur non possede.
		/// Nul si non branche -> panneau Atmosphere affiche un message d'attente.
		void SetDayNightCycle(engine::render::DayNightCycle* dayNight) { m_dayNight = dayNight; }

		/// Branche le cache de vignettes (possede par Engine). Pointeur non
		/// possede. Si nul, les vignettes sont rendues comme cellules grises
		/// (ImGui::Dummy 48x48) — l'UI reste fonctionnelle, juste sans previews.
		void SetTexturePreviewCache(engine::editor::TexturePreviewCache* cache) { m_texturePreviewCache = cache; }

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
		engine::core::Config* m_cfg = nullptr;
		engine::render::DayNightCycle* m_dayNight = nullptr;
		engine::editor::TexturePreviewCache* m_texturePreviewCache = nullptr;
		engine::editor::world::WorldEditorShell* m_shell = nullptr;
		/// Dialog modal Zone Presets (M100.46 incrément 3). PIMPL via
		/// unique_ptr pour éviter d'exposer le header dans l'API publique
		/// du WorldEditorImGui (cycle Shell ↔ dialog).
		std::unique_ptr<engine::editor::world::zone_presets::ZonePresetDialog> m_zonePresetDialog;
		bool m_showTextureLibrary = false;  // pilote par le menu Affichage (Task 14)
		/// Flag de visibilité du panneau « Atmosphere » (cycle jour/nuit
		/// + couleurs ciel + ambient). Par défaut visible. Pilote par
		/// l'entrée menu `Vue > Atmosphere`. Sans ce toggle, le panneau
		/// ne peut pas être ré-ouvert une fois fermé via la croix du
		/// dock — d'où la régression signalée utilisateur après les fix
		/// dual-menu (#622) qui ont supprimé l'ancienne barre M100.1
		/// listant tous les panels.
		bool m_showAtmospherePanel = true;
		/// Flag traçant si une tentative de pose de la disposition par défaut (DockBuilder) a déjà
		/// été faite. Reset à false au démarrage et lors d'un « Réinitialiser la disposition »,
		/// repassé à true après la pose.
		bool m_defaultLayoutAttempted = false;
		/// Dimensions du dockspace au dernier frame BuildUi. Sert a detecter un
		/// resize de fenetre pour forcer DockBuilderSetNodeSize sur le node racine
		/// (sinon les panneaux dockes restent ancres a l'ancienne taille apres
		/// un drag de bord de fenetre, donnant une UI vide ou hors viewport).
		float m_lastDockSpaceWidth  = 0.0f;
		float m_lastDockSpaceHeight = 0.0f;
#if defined(_WIN32)
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
#endif
	};

} // namespace engine::editor
