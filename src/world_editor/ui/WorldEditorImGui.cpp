#include "src/world_editor/ui/WorldEditorImGui.h"

#include <cstring>  // std::strcmp — filtrage du panneau « Scene » dans le menu View

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/world_editor/ui/TextureLibraryPanel.h"
#include "src/world_editor/ui/TexturePreviewCache.h"
#include "src/world_editor/ui/TreeSpeciesCatalog.h"
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/modes/EditorModeRegistry.h"
#include "src/world_editor/prefs/UserPrefsStore.h"
#include "src/world_editor/actions/EditorActionRegistry.h"
#include "src/world_editor/ui/ToolbarIconAtlas.h" // libellé FR de l'outil actif (barre de statut)
#include "src/world_editor/ui/CommandPaletteModel.h" // filtrage pur de la palette Ctrl+P (PR 3)
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/zone_presets/ZonePresetDialog.h"
// Lot C vague 4 — assistant « Nouvelle zone » : exécution d'un ZonePreset résolu
// par le wizard via le MÊME chemin que le ZonePresetDialog (executor + dispatch
// context construit depuis les outils/catalogs du Shell).
#include "src/world_editor/zone_presets/OperationDispatcher.h"
#include "src/world_editor/zone_presets/ZonePreset.h"
#include "src/world_editor/volumes/arches/ArchTool.h"
#include "src/world_editor/volumes/caves/CaveTool.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"
#include "src/world_editor/volumes/overhangs/OverhangTool.h"
#include "src/client/render/DayNightCycle.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Window.h"
#include "src/client/render/SharedFontHandles.h"
#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/render/vk/VkDeviceContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

#	include "imgui.h"
#	include "imgui_internal.h" // DockBuilder* (public mais declare dans l'en-tete internal)
#	include "imgui_impl_vulkan.h"
#	include "imgui_impl_win32.h"
	// ImGui 1.91+ : la declaration n'est plus dans l'en-tete (#if 0) pour eviter HWND dans l'API publique.
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#	include <vulkan/vulkan.h>
#endif

namespace engine::editor
{
	void WorldEditorImGui::SetEditorContext(WorldEditorSession* session, engine::core::Config* cfg)
	{
		m_session = session;
		m_cfg = cfg;
		// Instancie le dialog Zone Presets (M100.46 incrément 3) à la
		// première mise en place du contexte éditeur. Toujours créé pour
		// que l'entrée menu Fichier soit cliquable dès que le Shell est
		// branché en plus.
		if (!m_zonePresetDialog)
		{
			m_zonePresetDialog =
				std::make_unique<engine::editor::world::zone_presets::ZonePresetDialog>();
		}
	}

#if defined(_WIN32)
	namespace
	{
		/// Polish UI 2026-07-17 — version de la disposition de fenêtres par
		/// défaut. À BUMPER à chaque évolution de la disposition (ajout de
		/// panneau docké, réorganisation des nodes…) : les profils dont
		/// `user_prefs.json` porte une version différente voient leur
		/// disposition reconstruite automatiquement au boot (les .ini
		/// périmés éparpillaient les nouvelles fenêtres en flottant).
		constexpr int kEditorLayoutVersion = 2;

		void TryPersistMovementLayoutToUserSettings(std::string_view layout)
		{
			const char* path = "user_settings.json";
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				return;
			}
			std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string needle = "\"movement_layout\": \"";
			const size_t start = json.find(needle);
			if (start == std::string::npos)
			{
				return;
			}
			const size_t valueStart = start + needle.size();
			const size_t valueEnd = json.find('"', valueStart);
			if (valueEnd == std::string::npos)
			{
				return;
			}
			json.replace(valueStart, valueEnd - valueStart, layout);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				LOG_WARN(Core, "[WorldEditor] Ecriture impossible : {} (deplacement non persiste)", path);
				return;
			}
			out << json;
		}

		void CheckVk(VkResult err)
		{
			if (err == VK_SUCCESS)
			{
				return;
			}
			LOG_ERROR(Render, "[WorldEditorImGui] Vulkan erreur: {}", static_cast<int>(err));
		}

		bool BeginDynamicRenderingUi(VkCommandBuffer cmd,
			VkImageView backbufferView,
			VkExtent2D ext,
			const engine::render::VkDeviceContext& ctx,
			bool& outUsedKhr)
		{
			outUsedKhr = false;
			VkRenderingAttachmentInfo colorAttachment{};
			colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			colorAttachment.imageView = backbufferView;
			colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

			VkRenderingAttachmentInfoKHR colorAttachmentKHR{};
			colorAttachmentKHR.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
			colorAttachmentKHR.imageView = backbufferView;
			colorAttachmentKHR.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachmentKHR.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachmentKHR.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

			VkRenderingInfo renderingInfo{};
			renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderingInfo.renderArea.offset = { 0, 0 };
			renderingInfo.renderArea.extent = ext;
			renderingInfo.layerCount = 1;
			renderingInfo.colorAttachmentCount = 1;
			renderingInfo.pColorAttachments = &colorAttachment;

			VkRenderingInfoKHR renderingInfoKHR{};
			renderingInfoKHR.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
			renderingInfoKHR.renderArea.offset = { 0, 0 };
			renderingInfoKHR.renderArea.extent = ext;
			renderingInfoKHR.layerCount = 1;
			renderingInfoKHR.colorAttachmentCount = 1;
			renderingInfoKHR.pColorAttachments = &colorAttachmentKHR;

			const PFN_vkCmdBeginRendering pfnBeginCore = ctx.GetCmdBeginRenderingCore();
			const PFN_vkCmdEndRendering pfnEndCoreStored = ctx.GetCmdEndRenderingCore();
			const PFN_vkCmdBeginRenderingKHR pfnBeginKHR = ctx.GetCmdBeginRenderingKHR();
			const PFN_vkCmdEndRenderingKHR pfnEndKHRStored = ctx.GetCmdEndRenderingKHR();

			if (pfnBeginKHR && pfnEndKHRStored)
			{
				pfnBeginKHR(cmd, &renderingInfoKHR);
				outUsedKhr = true;
				return true;
			}
			if (pfnBeginCore && pfnEndCoreStored)
			{
				pfnBeginCore(cmd, &renderingInfo);
				outUsedKhr = false;
				return true;
			}
			return false;
		}

		void EndDynamicRenderingUi(VkCommandBuffer cmd,
			const engine::render::VkDeviceContext& ctx,
			bool usedKhr)
		{
			const PFN_vkCmdEndRendering pfnEndCore = ctx.GetCmdEndRenderingCore();
			const PFN_vkCmdEndRenderingKHR pfnEndKHR = ctx.GetCmdEndRenderingKHR();
			if (usedKhr && pfnEndKHR)
			{
				pfnEndKHR(cmd);
			}
			else if (!usedKhr && pfnEndCore)
			{
				pfnEndCore(cmd);
			}
		}
	} // namespace
#endif

#if defined(_WIN32)
	namespace
	{
		// Plafond du nombre de lignes de grille par axe (surimpression ImGui). Sans cela, une maille
		// fine sur un grand terrain genere trop de primitives ; un plafond trop bas rend la maille
		// "figee" (espacement minimal ~= tailleTerrain / (plafond - 1)).
		constexpr int kWorldEditorGridMaxLinesPerAxis = 2048;

		bool WorldToScreen(const float vp[16], float wx, float wy, float wz, int vw, int vh, float& sx, float& sy)
		{
			const float cx = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
			const float cy = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
			const float cz = vp[2] * wx + vp[6] * wy + vp[10] * wz + vp[14];
			const float cw = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];
			if (cw <= 1e-5f)
			{
				return false;
			}
			const float invW = 1.0f / cw;
			const float ndcX = cx * invW;
			const float ndcY = cy * invW;
			const float ndcZ = cz * invW;
			if (ndcZ < 0.0f || ndcZ > 1.0f)
			{
				return false;
			}
			sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(vw);
			// PR25 (M??.?) : fix Y-flip de la grille editeur. Le code precedent
			// faisait `sy = (1 - (ndc * 0.5 + 0.5)) * vh` (= `(0.5 - 0.5 * ndc) * vh`),
			// ce qui correspond a la convention OpenGL (NDC.y +1 = haut ecran).
			// Mais le rendu 3D du moteur utilise Vulkan : `Mat4::PerspectiveVulkan`
			// pose deja `m[5] = -t` pour inverser Y vers la convention Vulkan
			// (NDC.y +1 = bas ecran). Le `1.0f -` ici annulait ce flip et faisait
			// rendre la grille avec un Y inverse par rapport au sol/ciel : sur la
			// capture de validation, l'utilisateur observait DEUX horizons distincts
			// (transition rouge/bleu en haut + point de fuite de la grille en bas).
			// Fix : retirer le `1.0f -`. La grille s'aligne maintenant avec le rendu 3D.
			// Affecte aussi le brush preview (cercle orange) qui partage WorldToScreen.
			sy = (ndcY * 0.5f + 0.5f) * static_cast<float>(vh);
			return true;
		}

		bool TerrainWorldY(const engine::render::terrain::HeightmapData* hm,
			float ox, float oz, float ws, float hScale, float wx, float wz, float& yOut)
		{
			if (!hm || hm->width == 0 || hm->height == 0 || ws <= 0.0f)
			{
				return false;
			}
			const float u = (wx - ox) / ws;
			const float v = (wz - oz) / ws;
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				return false;
			}
			yOut = hm->SampleBilinearNorm(u, v) * hScale;
			return true;
		}

		void DrawSegmentedWorldLine(ImDrawList* dl, const float vp[16], int vw, int vh, ImU32 col,
			float x0, float y0, float z0, float x1, float y1, float z1, int segments)
		{
			if (segments < 1)
			{
				return;
			}
			ImVec2 prev{};
			bool has = false;
			for (int i = 0; i <= segments; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(segments);
				const float x = x0 + (x1 - x0) * t;
				const float y = y0 + (y1 - y0) * t;
				const float z = z0 + (z1 - z0) * t;
				float sx, sy;
				if (WorldToScreen(vp, x, y, z, vw, vh, sx, sy))
				{
					if (has)
					{
						dl->AddLine(prev, ImVec2(sx, sy), col);
					}
					prev = ImVec2(sx, sy);
					has = true;
				}
				else
				{
					has = false;
				}
			}
		}

		void DrawViewportOverlaysImpl(const WorldEditorViewportOverlayDesc& d)
		{
			if (!d.viewProjColMajor || d.viewportWidth <= 0 || d.viewportHeight <= 0)
			{
				return;
			}
			// Background draw list (et non Foreground) pour que la grille et
			// les marqueurs d'overlay viewport 3D soient **dessinés sous les
			// fenêtres ImGui dockées** (panneaux Outils, Inspector, Tool
			// Properties, etc.). En Foreground la grille débordait sur toutes
			// les fenêtres et rendait l'UI illisible. L'ordre Z final est :
			//   Vulkan terrain (fond) → BackgroundDrawList (grille, brush
			//   preview, marqueurs) → fenêtres ImGui (recouvrent) →
			//   ForegroundDrawList (reservé aux popups critiques).
			ImDrawList* dl = ImGui::GetBackgroundDrawList();
			const float* vp = d.viewProjColMajor;
			const int vw = d.viewportWidth;
			const int vh = d.viewportHeight;
			const float ox = d.terrainOriginX;
			const float oz = d.terrainOriginZ;
			const float ws = d.terrainWorldSize;
			const float cell = std::max(0.5f, d.gridCellMeters);
			const ImU32 gridCol = IM_COL32(180, 220, 255, 110);
			const ImU32 brushCol = IM_COL32(255, 200, 80, 200);

			if (d.showGrid && d.heightmap && ws > 0.0f)
			{
				int nz = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nz > kWorldEditorGridMaxLinesPerAxis)
				{
					nz = kWorldEditorGridMaxLinesPerAxis;
				}
				int nx = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nx > kWorldEditorGridMaxLinesPerAxis)
				{
					nx = kWorldEditorGridMaxLinesPerAxis;
				}
				const int lineSegments = std::clamp(200000 / std::max(1, nx + nz), 8, 48);
				const float stepZ = ws / static_cast<float>(std::max(1, nz - 1));
				for (int iz = 0; iz < nz; ++iz)
				{
					const float z = oz + static_cast<float>(iz) * stepZ;
					float y0 = 0.f, y1 = 0.f;
					if (!TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, ox, z, y0)
						|| !TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, ox + ws, z, y1))
					{
						continue;
					}
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, ox, y0, z, ox + ws, y1, z, lineSegments);
				}
				const float stepX = ws / static_cast<float>(std::max(1, nx - 1));
				for (int ix = 0; ix < nx; ++ix)
				{
					const float x = ox + static_cast<float>(ix) * stepX;
					float y0 = 0.f, y1 = 0.f;
					if (!TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, x, oz, y0)
						|| !TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, x, oz + ws, y1))
					{
						continue;
					}
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, x, y0, oz, x, y1, oz + ws, lineSegments);
				}
			}

			if (d.layoutInstancesOverlay != nullptr)
			{
				const ImU32 colSel = IM_COL32(255, 90, 255, 230);
				const ImU32 colNorm = IM_COL32(90, 255, 140, 200);
				for (size_t ii = 0; ii < d.layoutInstancesOverlay->size(); ++ii)
				{
					const engine::editor::WorldMapEditLayoutInstance& inst = (*d.layoutInstancesOverlay)[ii];
					const float wx = static_cast<float>(inst.worldX);
					const float wy = static_cast<float>(inst.worldY);
					const float wz = static_cast<float>(inst.worldZ);
					float sx = 0.f;
					float sy = 0.f;
					if (WorldToScreen(vp, wx, wy, wz, vw, vh, sx, sy))
					{
						const ImU32 c = (static_cast<int>(ii) == d.selectedLayoutInstanceOverlay) ? colSel : colNorm;
						dl->AddCircleFilled(ImVec2(sx, sy), 7.f, c, 16);
						dl->AddCircle(ImVec2(sx, sy), 8.f, IM_COL32(255, 255, 255, 160), 16, 1.5f);
					}
				}
			}

			if (d.showBrushPreview && d.heightmap && d.brushRadiusMeters > 0.0f)
			{
				float cy = 0.f;
				if (TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, d.brushWorldX, d.brushWorldZ, cy))
				{
					float sx0, sy0, sx1, sy1;
					if (WorldToScreen(vp, d.brushWorldX, cy, d.brushWorldZ, vw, vh, sx0, sy0))
					{
						float yEdge = cy;
						const float wx1 = d.brushWorldX + d.brushRadiusMeters;
						(void)TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, wx1, d.brushWorldZ, yEdge);
						if (WorldToScreen(vp, wx1, yEdge, d.brushWorldZ, vw, vh, sx1, sy1))
						{
							const float dx = sx1 - sx0;
							const float dy = sy1 - sy0;
							const float radPix = std::sqrt(dx * dx + dy * dy);
							if (radPix > 1.5f)
							{
								dl->AddCircle(ImVec2(sx0, sy0), radPix, brushCol, 0, 2.0f);
							}
						}
					}
				}
			}
		}
	} // namespace
#endif

	// Ctor + dtor out-of-line : la définition de `ZonePresetDialog` doit
	// être visible ici pour que `std::unique_ptr<ZonePresetDialog>` puisse
	// générer son cleanup d'exception. L'`#include` au top du fichier
	// fournit le type complet.
	WorldEditorImGui::WorldEditorImGui() = default;

	WorldEditorImGui::~WorldEditorImGui()
	{
#if defined(_WIN32)
		if (m_ready)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Shutdown non appele avant destruction");
		}
#endif
	}

	bool WorldEditorImGui::Init(VkInstance instance,
		const engine::render::VkDeviceContext& deviceContext,
		VkFormat swapchainFormat,
		uint32_t swapchainImageCount,
		uint32_t vulkanApiVersion,
		void* hwndNative,
		const engine::core::Config* cfg,
		bool isWorldEditorExe)
	{
#if !defined(_WIN32)
		(void)instance;
		(void)deviceContext;
		(void)swapchainFormat;
		(void)swapchainImageCount;
		(void)vulkanApiVersion;
		(void)hwndNative;
		(void)cfg;
		(void)isWorldEditorExe;
		return false;
#else
		if (m_ready)
		{
			return true;
		}
		if (!deviceContext.IsValid() || !deviceContext.SupportsDynamicRendering())
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignore: device ou dynamic rendering indisponible");
			return false;
		}
		HWND hwnd = hwndNative ? static_cast<HWND>(hwndNative) : nullptr;
		if (!hwnd)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignore: HWND nul");
			return false;
		}
		const uint32_t imgCount = std::max(2u, swapchainImageCount);

		// 1024 sets : la fonte ImGui + l'apercu de race + les icones de competences
		// (arbre/Grimoire/barre via SkillIconCache) allouent chacun 1 descripteur.
		// L'arbre peut afficher ~180 icones a la fois ; 64 (valeur initiale) etait
		// trop juste. SkillIconCache plafonne a 900 (< 1024) pour rester sous cette
		// borne. FREE_DESCRIPTOR_SET_BIT conserve pour RemoveTexture.
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 1024;
		poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(deviceContext.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WorldEditorImGui] vkCreateDescriptorPool a echoue");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = "world_editor_imgui.ini";
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Pas de NavEnableKeyboard : sinon io.WantCaptureKeyboard reste souvent vrai et bloque WASD (camera FPS).
		ImGui::StyleColorsDark();

		// Polices auth (Windlass / Morpheus) chargees dans l'atlas ImGui AVANT ImGui_ImplVulkan_Init -
		// celui-ci construit la texture des fonts une seule fois a partir de l'atlas. Sans ce chargement,
		// l'UI auth utilise la police ImGui par defaut (ProggyClean ~13 px) qui ne ressemble pas a la
		// maquette Lune Noire. La piste Vulkan/AuthGlyphPass utilise deja ces memes fichiers (Engine.cpp).
		// On charge Windlass en premier : elle devient la police par defaut d'ImGui.
		// Range restreint pour les fontes decoratives Lune Noire (Windlass) : A-Z, a-z,
		// 0-9, espace, et ponctuation basique presente dans la fonte. Tout le reste est
		// laisse a ProggyClean en MergeMode (cf. plus bas) - sinon ImGui reserve un slot
		// vide pour chaque codepoint demande (ex. % et [ et ]) et le merge ne remplace
		// jamais les slots existants, ce qui produisait des "?" a l'affichage.
		static const ImWchar kWindlassRanges[] = {
			0x0020, 0x0020, // espace
			0x0027, 0x0027, // '
			0x002C, 0x003F, // , - . / 0-9 : ; < = > ?
			0x0041, 0x005A, // A-Z
			0x0061, 0x007A, // a-z
			0,
		};
		// Plage etendue pour la fonte de repli riche (Arial). Couvre tout ce que
		// Windlass (decorative, A-Z/a-z/0-9 uniquement) ne fournit pas :
		//   - symboles ASCII : * [ ] @ % { } etc. ;
		//   - supplement Latin-1 : accents FR (é è à ç û ï ô...) ;
		//   - ponctuation generale : tiret cadratin/demi-cadratin (— –), apostrophes
		//     et guillemets courbes (' ' " "), puce (•), points de suspension (…) ;
		//   - euro et quelques fleches.
		// Sans ces plages, ImGui rend '?' pour tout caractere non-ASCII — c'est la
		// cause des '?' visibles dans TOUTES les interfaces in-game (titres de
		// panneaux, separateurs, libelles accentues).
		static const ImWchar kRichFallbackRanges[] = {
			0x0020, 0x00FF, // ASCII + supplement Latin-1 (symboles + accents FR)
			0x2010, 0x2027, // tirets, apostrophes/guillemets courbes, puce, points de suspension
			0x20AC, 0x20AC, // € (euro)
			0x2190, 0x2193, // fleches gauche/haut/droite/bas
			0,
		};
		auto loadAuthFontFromConfig = [&io, cfg](std::string_view relativePath, float pixelHeight, const char* role) -> bool {
			if (cfg == nullptr || relativePath.empty())
			{
				return false;
			}
			std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(*cfg, relativePath);
			if (bytes.empty())
			{
				LOG_WARN(Render, "[WorldEditorImGui] Police {} introuvable ou vide : {}", role, relativePath);
				return false;
			}
			// L'atlas ImGui prend la propriete du buffer (FontDataOwnedByAtlas par defaut = true) et le
			// liberera via IM_FREE - on doit donc l'allouer via IM_ALLOC, pas reutiliser le std::vector
			// (sinon UB : la memoire serait liberee deux fois ou utilisee apres liberation).
			void* atlasOwned = IM_ALLOC(bytes.size());
			std::memcpy(atlasOwned, bytes.data(), bytes.size());
			ImFontConfig fcfg{};
			ImFont* font = io.Fonts->AddFontFromMemoryTTF(atlasOwned, static_cast<int>(bytes.size()), pixelHeight,
				&fcfg, kWindlassRanges);
			if (font == nullptr)
			{
				IM_FREE(atlasOwned);
				LOG_WARN(Render, "[WorldEditorImGui] Police {} : AddFontFromMemoryTTF a echoue pour {}", role, relativePath);
				return false;
			}
			LOG_INFO(Render, "[WorldEditorImGui] Police {} chargee dans l'atlas ImGui : {} ({}px)", role, relativePath, pixelHeight);
			return true;
		};

		if (cfg != nullptr)
		{
			// Editeur monde (lcdlln_world_editor.exe) : on substitue Arial a Windlass
			// comme police par defaut. Windlass est decorative (faite pour l'UI auth /
			// in-game qui evoque le lore Lune Noire) — illisible et limitee en glyphs
			// pour un editeur de carte technique. Arial est neutre, riche en glyphs
			// (accents, ponctuation), et standard sur Windows (C:/Windows/Fonts/arial.ttf).
			//
			// Sequence : si isWorldEditorExe ET le fichier Arial est lisible, on charge
			// Arial en PREMIER (devient ImGui::GetFont() par defaut). Le bloc Windlass
			// existant en dessous est court-circuite par un flag local pour eviter que
			// Windlass se retrouve par-dessus dans l'atlas. Si Arial absent, fallback
			// Windlass comme avant (degradation gracieuse).
			bool arialLoaded = false;
			if (isWorldEditorExe)
			{
				const std::string arialPath = cfg->GetString("editor.font.arial_path", "C:/Windows/Fonts/arial.ttf");
				const float arialPx = static_cast<float>(std::clamp<int64_t>(
					cfg->GetInt("editor.font.arial_pixel_height", 14), 11, 32));
				// arial_path : chemin absolu (defaut C:/Windows/Fonts/arial.ttf) OU
				// chemin relatif a paths.content. On tente d'abord absolu puis content.
				std::vector<uint8_t> bytesArial = engine::platform::FileSystem::ReadAllBytes(std::filesystem::path(arialPath));
				if (bytesArial.empty())
				{
					bytesArial = engine::platform::FileSystem::ReadAllBytesContent(*cfg, arialPath);
				}
				if (!bytesArial.empty())
				{
					void* atlasArial = IM_ALLOC(bytesArial.size());
					std::memcpy(atlasArial, bytesArial.data(), bytesArial.size());
					ImFontConfig acfg{};
					// Plage riche (A-Z, a-z, 0-9, accents FR, ponctuation typographique
					// — tiret cadratin, guillemets courbes — et fleches) : evite les '?'
					// dans l'editeur aussi (meme cause racine que l'in-game).
					ImFont* arialFont = io.Fonts->AddFontFromMemoryTTF(atlasArial, static_cast<int>(bytesArial.size()),
						arialPx, &acfg, kRichFallbackRanges);
					if (arialFont != nullptr)
					{
						arialLoaded = true;
						LOG_INFO(Render, "[WorldEditorImGui] Police editeur Arial chargee : {} ({}px)", arialPath, arialPx);
					}
					else
					{
						IM_FREE(atlasArial);
						LOG_WARN(Render, "[WorldEditorImGui] AddFontFromMemoryTTF Arial a echoue : {}", arialPath);
					}
				}
				else
				{
					LOG_WARN(Render, "[WorldEditorImGui] Police editeur Arial introuvable : {} (fallback Windlass)", arialPath);
				}
			}

			// Cles specifiques a la piste ImGui (la piste Vulkan/AuthGlyphPass garde sa propre
			// taille via render.auth_ui.font_pixel_height = 28). En ImGui les facteurs
			// SetWindowFontScale (1.62 pour le titre, 1.12 pour le sous-titre, 1.15 pour le titre
			// du panneau, 0.78-0.95 pour les libelles...) sont calibres pour la police par defaut
			// ProggyClean ~13 px ; charger Windlass a la meme taille respecte donc tous les
			// gabarits existants. On peut surcharger via render.auth_ui.imgui.font_pixel_height
			// si on veut grossir/reduire globalement l'UI auth ImGui sans toucher aux scales.
			//
			// arialLoaded=true (editeur monde) court-circuite Windlass pour qu'Arial reste
			// la police par defaut. L'editeur n'utilise pas les ecrans auth, donc Windlass
			// n'a pas d'usage la-bas.
			const std::string uiFontPath = cfg->GetString("render.auth_ui.font_path", "");
			const float uiFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.font_pixel_height", 13), 11, 32));
			if (!arialLoaded)
			{
				loadAuthFontFromConfig(uiFontPath, uiFontPx, "UI");
			}

			// Fallback merge : Windlass.ttf ne contient pas les caracteres accentues, '*' (utilise
			// par ImGuiInputTextFlags_Password), '[' ']' '%' '@' et autres ponctuations etendues.
			// ImGui les rendait alors comme '?'. On merge ProggyClean (ImGui built-in) IMMEDIATEMENT
			// apres Windlass (sinon le merge prend la fonte precedente, pas Windlass) : Windlass
			// reste prioritaire pour A-Z/a-z/0-9 et ProggyClean prend le relais pour les autres.
			// Visuellement les glyphes de fallback ne matchent pas la maquette mais c'est mieux
			// que des '?'.
			//
			// Si arialLoaded (editeur monde), on saute ProggyClean : Arial couvre deja les
			// accents et ponctuations etendues, pas besoin de fallback.
			if (!arialLoaded)
			{
				// Fonte de repli pour tous les glyphes hors Windlass. On merge en
				// priorite Arial (Latin-1 complet + ponctuation typographique, present
				// sur tout Windows) : c'est ce qui elimine les '?' affiches pour les
				// accents et le tiret cadratin dans TOUTES les interfaces in-game.
				// Si Arial est introuvable, repli sur ProggyClean (ASCII seulement —
				// couvre * [ ] @ mais laisse accents/typographie en '?').
				bool richFallbackLoaded = false;
				const std::string fbArialPath = cfg->GetString("editor.font.arial_path", "C:/Windows/Fonts/arial.ttf");
				std::vector<uint8_t> bytesFbArial = engine::platform::FileSystem::ReadAllBytes(std::filesystem::path(fbArialPath));
				if (bytesFbArial.empty())
				{
					bytesFbArial = engine::platform::FileSystem::ReadAllBytesContent(*cfg, fbArialPath);
				}
				if (!bytesFbArial.empty())
				{
					// L'atlas prend la propriete du buffer (IM_FREE) → allouer via IM_ALLOC.
					void* atlasFbArial = IM_ALLOC(bytesFbArial.size());
					std::memcpy(atlasFbArial, bytesFbArial.data(), bytesFbArial.size());
					ImFontConfig fbCfg{};
					fbCfg.MergeMode = true; // fusionne dans la fonte Windlass par defaut
					// Taille alignee sur la fonte UI Windlass pour une hauteur coherente.
					ImFont* fbFont = io.Fonts->AddFontFromMemoryTTF(atlasFbArial, static_cast<int>(bytesFbArial.size()),
						uiFontPx, &fbCfg, kRichFallbackRanges);
					if (fbFont != nullptr)
					{
						richFallbackLoaded = true;
						LOG_INFO(Render, "[WorldEditorImGui] Fallback Arial merge sur Windlass (accents FR + ponctuation typographique)");
					}
					else
					{
						IM_FREE(atlasFbArial);
						LOG_WARN(Render, "[WorldEditorImGui] Fallback Arial : AddFontFromMemoryTTF a echoue ({})", fbArialPath);
					}
				}
				if (!richFallbackLoaded)
				{
					ImFontConfig fallbackCfg{};
					fallbackCfg.MergeMode = true;
					io.Fonts->AddFontDefault(&fallbackCfg);
					LOG_INFO(Render, "[WorldEditorImGui] Fallback ProggyClean merge sur Windlass (Arial absent ; accents/typographie restent en '?')");
				}
			}

			const std::string valueFontPath = cfg->GetString("render.auth_ui.value_font_path", "");
			const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.value_font_pixel_height", 12), 11, 32));
			if (!arialLoaded)
			{
				// Police "valeurs" Morpheus : utile uniquement pour l'UI auth (nom du
				// perso, montant en or...). Pas pertinent dans l'editeur monde.
				loadAuthFontFromConfig(valueFontPath, valueFontPx, "valeurs");
			}

			// Fonte password : 2eme passe sur Windlass a 24 px (plus large que la
			// fonte UI standard 13 px), avec un merge ProggyClean immediat pour le
			// glyph '*' qui n'est pas dans Windlass. Le pointeur est partage via
			// SharedFontHandles::g_largePasswordFont pour que DrawAuthGoldField
			// puisse PushFont autour de l'InputText password.
			//
			// Skipped en mode editeur monde : pas d'ecran auth, pas d'InputText
			// password. Le pointeur partage reste a nullptr.
			if (!arialLoaded && !uiFontPath.empty())
			{
				std::vector<uint8_t> bytesPwd = engine::platform::FileSystem::ReadAllBytesContent(*cfg, uiFontPath);
				if (!bytesPwd.empty())
				{
					void* atlasPwd = IM_ALLOC(bytesPwd.size());
					std::memcpy(atlasPwd, bytesPwd.data(), bytesPwd.size());
					ImFontConfig pwdCfg{};
					ImFont* pwdFont = io.Fonts->AddFontFromMemoryTTF(atlasPwd, static_cast<int>(bytesPwd.size()),
						24.0f, &pwdCfg, kWindlassRanges);
					if (pwdFont != nullptr)
					{
						// Merge ProggyClean a 24px pour fournir '*' (et autres glyphs
						// hors range Windlass) a la meme taille que Windlass.
						// Note : ProggyClean est bitmap fixe a 13px, donc l'agrandissement
						// est crenele. Acceptable pour un masque password.
						ImFontConfig mergePwd{};
						mergePwd.MergeMode = true;
						mergePwd.SizePixels = 24.0f;
						io.Fonts->AddFontDefault(&mergePwd);
						engine::render::SharedFontHandles::g_largePasswordFont = static_cast<void*>(pwdFont);
						LOG_INFO(Render, "[WorldEditorImGui] Police password (Windlass 24px + ProggyClean merge) prete");
					}
					else
					{
						IM_FREE(atlasPwd);
					}
				}
			}
		}

		ImGui_ImplWin32_Init(hwnd);

		VkPipelineRenderingCreateInfoKHR pipelineRendering{};
		pipelineRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRendering.colorAttachmentCount = 1;
		pipelineRendering.pColorAttachmentFormats = &swapchainFormat;

		ImGui_ImplVulkan_InitInfo vulkanInfo{};
		vulkanInfo.ApiVersion = vulkanApiVersion;
		vulkanInfo.Instance = instance;
		vulkanInfo.PhysicalDevice = deviceContext.GetPhysicalDevice();
		vulkanInfo.Device = deviceContext.GetDevice();
		vulkanInfo.QueueFamily = deviceContext.GetGraphicsQueueFamilyIndex();
		vulkanInfo.Queue = deviceContext.GetGraphicsQueue();
		vulkanInfo.DescriptorPool = m_descriptorPool;
		vulkanInfo.MinImageCount = imgCount;
		vulkanInfo.ImageCount = imgCount;
		vulkanInfo.PipelineCache = VK_NULL_HANDLE;
		vulkanInfo.Subpass = 0;
		vulkanInfo.DescriptorPoolSize = 0;
		vulkanInfo.UseDynamicRendering = true;
		vulkanInfo.PipelineRenderingCreateInfo = pipelineRendering;
		vulkanInfo.Allocator = nullptr;
		vulkanInfo.CheckVkResultFn = CheckVk;
		vulkanInfo.MinAllocationSize = 0;

		if (!ImGui_ImplVulkan_Init(&vulkanInfo))
		{
			LOG_ERROR(Render, "[WorldEditorImGui] ImGui_ImplVulkan_Init a echoue");
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			vkDestroyDescriptorPool(deviceContext.GetDevice(), m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
			return false;
		}

		m_ready = true;
		LOG_INFO(Render, "[WorldEditorImGui] Init OK (ImGui + Vulkan dynamic rendering)");
		return true;
#endif
	}

	void WorldEditorImGui::Shutdown(VkDevice device)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}
		vkDeviceWaitIdle(device);
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		if (m_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		m_ready = false;
		LOG_INFO(Render, "[WorldEditorImGui] Shutdown OK");
#else
		(void)device;
#endif
	}

	void WorldEditorImGui::OnSwapchainRecreate(uint32_t swapchainImageCount)
	{
#if defined(_WIN32)
		if (m_ready && swapchainImageCount > 0)
		{
			ImGui_ImplVulkan_SetMinImageCount(std::max(2u, swapchainImageCount));
		}
#else
		(void)swapchainImageCount;
#endif
	}

	void WorldEditorImGui::NewFrame(float deltaSeconds, float displayWidth, float displayHeight)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(displayWidth, displayHeight);
		io.DeltaTime = deltaSeconds > 1e-6f ? deltaSeconds : 1.f / 60.f;
#else
		(void)deltaSeconds;
		(void)displayWidth;
		(void)displayHeight;
#endif
	}

	void WorldEditorImGui::BuildUi(const WorldEditorViewportOverlayDesc* viewportOverlay)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}

		// Lot C vague 4 — Enregistre les règles de validation MVP une seule fois
		// (le registre prend l'ownership : réenregistrer empilerait des doublons).
		if (!m_validationRegistered)
		{
			engine::editor::world::validation::RegisterMvpValidationRules(m_validationRegistry);
			m_validationRegistered = true;
		}

		// Lot C vague 4 — Idem pour les règles de diagnostic workflow MVP (mêmes
		// raisons : ownership pris par le registre, enregistrement idempotent).
		if (!m_diagnosticRegistered)
		{
			engine::editor::world::diagnostic::RegisterMvpDiagnosticRules(m_diagnosticRegistry);
			m_diagnosticRegistered = true;
		}

		// Lot C vague 4 — Début de frame : vide le registre de rectangles-cibles.
		// Chaque widget-cible (bouton Valider, entrée menu export) réenregistre son
		// rectangle après son rendu, plus bas dans cette frame.
		m_widgetTargets.Clear();

		// Réorganisation UI 2026-07-17 — les actions sont enregistrées une
		// fois dans le registre du shell, puis la barre de menu française,
		// la modale Quitter et les fenêtres Préférences / À propos sont
		// rendues depuis ce registre (spec docs/superpowers/specs/
		// 2026-07-17-editor-menus-toolbar-reorg-design.md).
		RegisterEditorActions();
		// Polish UI 2026-07-17 — disposition versionnée : si le profil
		// utilisateur vient d'une version de disposition antérieure (ou n'en
		// a jamais vue), on reconstruit automatiquement la disposition par
		// défaut au lieu de laisser l'ancien .ini éparpiller les nouvelles
		// fenêtres en flottant (bug constaté au premier build mergé).
		if (!m_layoutVersionChecked)
		{
			m_layoutVersionChecked = true;
			auto& prefsStore = engine::editor::world::prefs::UserPrefsStore::Instance();
			const int seenVersion = prefsStore.IsInitialized()
				? prefsStore.GetLayoutVersion() : kEditorLayoutVersion;
			if (seenVersion != kEditorLayoutVersion)
			{
				ResetDockLayout();
				prefsStore.SetLayoutVersion(kEditorLayoutVersion);
				LOG_INFO(EditorWorld,
					"[WorldEditorImGui] Disposition reconstruite (version {} -> {})",
					seenVersion, kEditorLayoutVersion);
			}
		}
		// PR 3 — Ctrl+P ouvre la palette de commandes (détection ImGui
		// directe : pas de plumbing Engine nécessaire).
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
		{
			OpenCommandPalette();
		}
		RenderMenuBarFr();
		RenderQuitConfirmModal();
		RenderPreferencesWindow();
		RenderAboutWindow();
		RenderCommandPalette();
		RenderShortcutsWindow();

		// M100.46 incrément 3 — dessine la popup modale Zone Presets
		// (no-op si non ouverte ou Shell non branché). m_cfg passé pour
		// les ops simulation (incrément 2e) : si null, les 4 ops sim
		// renverront Failed.
		if (m_zonePresetDialog && m_shell)
		{
			m_zonePresetDialog->Draw(*m_shell, m_cfg);
		}

		ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);
		ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
			| ImGuiWindowFlags_NoBackground;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		if (ImGui::Begin("WorldEditorDockSpaceHost", nullptr, hostFlags))
		{
			const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceV2");
			// PR #427 follow-up : cause racine du "terrain invisible". L'auteur initial avait
			// ecrit (1u << 4) en pensant que c'etait ImGuiDockNodeFlags_PassthruCentralNode.
			// Mais dans ImGui v1.91.9-docking (utilise via FetchContent CMakeLists.txt:167) :
			//   ImGuiDockNodeFlags_PassthruCentralNode = 1 << 3   (= 8)   <-- la bonne valeur
			//   ImGuiDockNodeFlags_NoDockingSplit      = 1 << 4   (= 16)  <-- celle utilisee par erreur
			// Sans PassthruCentralNode, le node central du DockSpace dessine un fond opaque qui
			// MASQUE le rendu 3D. La pipeline (terrain, lighting, tonemap) fonctionne, mais son
			// resultat n'est jamais visible.
			constexpr ImGuiDockNodeFlags kDockSpaceFlags = static_cast<ImGuiDockNodeFlags>(1u << 3);

			// Detection de resize de fenetre : si la taille du viewport a change
			// depuis la derniere frame (apres initialisation), on force
			// DockBuilderSetNodeSize sur le node racine pour que les panneaux dockes
			// se relayent automatiquement. Sans ce relayout, les panneaux restent
			// ancres a l'ancienne taille (lue dans world_editor_imgui.ini), ce qui
			// les place hors du viewport et donne l'impression que l'UI a disparu
			// apres un drag de bord de fenetre.
			//
			// IMPORTANT : on ne fire pas la premiere frame (m_lastDockSpaceWidth==0
			// par defaut) car le node racine n'existe pas encore (cree par le bloc
			// m_defaultLayoutAttempted juste au-dessus). DockBuilderSetNodeSize
			// avant que la layout par defaut ait ete posee bouilli les sizes
			// proportionnelles -> ecran noir signale par l'utilisateur.
			constexpr float kResizeEpsilonPx = 0.5f;
			const bool dockSizeInitialized = (m_lastDockSpaceWidth > kResizeEpsilonPx);
			if (dockSizeInitialized
			 && (std::fabs(vp->WorkSize.x - m_lastDockSpaceWidth)  > kResizeEpsilonPx
			  || std::fabs(vp->WorkSize.y - m_lastDockSpaceHeight) > kResizeEpsilonPx))
			{
				if (ImGui::DockBuilderGetNode(dockId) != nullptr)
				{
					ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
				}
			}
			m_lastDockSpaceWidth  = vp->WorkSize.x;
			m_lastDockSpaceHeight = vp->WorkSize.y;

			// Disposition par defaut : si ImGui n'a pas charge de layout depuis world_editor_imgui.ini
			// (premier demarrage, fichier supprime via le menu Vue, ou nouveau dockId), on pose une
			// disposition propre (palette gauche, inspecteur droite, scene centrale, statut bas) via
			// l'API DockBuilder. La tentative ne se fait qu'une fois par session - sinon, l'utilisateur
			// qui re-dispose ses panneaux les verrait reposer chaque frame.
			if (!m_defaultLayoutAttempted)
			{
				m_defaultLayoutAttempted = true;
				if (ImGui::DockBuilderGetNode(dockId) == nullptr)
				{
					ImGui::DockBuilderRemoveNode(dockId);
					ImGui::DockBuilderAddNode(dockId, kDockSpaceFlags | ImGuiDockNodeFlags_DockSpace);
					ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

					ImGuiID idLeft = 0;
					ImGuiID idCenter = dockId;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Left, 0.22f, &idLeft, &idCenter);

					ImGuiID idRight = 0;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Right, 0.30f, &idRight, &idCenter);

					ImGuiID idBottom = 0;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Down, 0.18f, &idBottom, &idCenter);

					// Polish UI 2026-07-17 — disposition UNIFIÉE : les panneaux
					// du SHELL (M100.x) sont dockés ici aussi, par nom de
					// fenêtre, dans les mêmes nodes que les fenêtres session
					// (M43.x). Fini les deux systèmes de disposition
					// superposés qui laissaient flotter la moitié des
					// fenêtres (capture utilisateur du build #976-979).

					// Gauche : la palette d'outils d'abord (onglet actif au
					// premier lancement), la fenêtre « Outils » (infos) derrière.
					ImGui::DockBuilderDockWindow("Palette d'outils", idLeft);
					ImGui::DockBuilderDockWindow("Outils", idLeft);

					// Droite : onglets carte / affichage / atmosphère /
					// import / objets + panneaux scène du shell.
					// Scene rejoint cette pile : la docker dans le node central annulait le passthrough et
					// bloquait l'interaction 3D (regression P1). En tant qu'onglet a droite, son contenu
					// (diagnostic camera) reste accessible et le node central reste vide -> 3D visible
					// et cliquable au travers du DockSpace.
					ImGui::DockBuilderDockWindow("Carte", idRight);
					ImGui::DockBuilderDockWindow("Affichage & grille", idRight);
					ImGui::DockBuilderDockWindow("Atmosphere", idRight);
					ImGui::DockBuilderDockWindow("Import assets", idRight);
					ImGui::DockBuilderDockWindow("Objets sur la carte", idRight);
					ImGui::DockBuilderDockWindow("Outliner", idRight);
					ImGui::DockBuilderDockWindow("Inspector", idRight);
					ImGui::DockBuilderDockWindow("Tool Properties", idRight);
					// Panneaux avancés masqués par défaut : pré-dockés pour
					// apparaître au bon endroit quand le menu Fenêtre les ouvre.
					ImGui::DockBuilderDockWindow("Quest Editor", idRight);
					ImGui::DockBuilderDockWindow("Building Editor", idRight);
					ImGui::DockBuilderDockWindow("Surface Table", idRight);
					ImGui::DockBuilderDockWindow("Collision Editor", idRight);
					ImGui::DockBuilderDockWindow("History", idRight);
					ImGui::DockBuilderDockWindow("Asset Browser", idBottom);
					ImGui::DockBuilderDockWindow("Console", idBottom);
					ImGui::DockBuilderDockWindow("Routines", idBottom);
					// La fenêtre « Camera (aide) » n'est PAS dockée — ses flags
					// `NoMouseInputs` polluent le tab bar du node quand elle
					// est dans le même groupe que d'autres tabs (cause des
					// onglets non cliquables, bug confirmé utilisateur).
					// Elle est opt-in via `Vue > Aide camera` et flotte.

					// Statut en bas, plein largeur (onglet actif du node bas).
					ImGui::DockBuilderDockWindow("Statut", idBottom);

					ImGui::DockBuilderFinish(dockId);
				}
			}

			// Belt-and-suspenders : on force aussi ImGuiCol_DockingEmptyBg a transparent. Avec le flag
			// PassthruCentralNode correct ci-dessus, ImGui ne devrait deja plus dessiner ce fond,
			// mais on le force a transparent au cas ou (couts: nul, securite: max).
			ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::DockSpace(dockId, ImVec2(0.f, 0.f), kDockSpaceFlags);
			ImGui::PopStyleColor();
		}
		ImGui::End();
		ImGui::PopStyleVar(3);

		// Fenêtre d'aide caméra (overlay texte avec instructions WASD,
		// position monde, etc.). Cachée par défaut depuis #629 parce que
		// son flag `NoMouseInputs` polluait le tab bar du dock node quand
		// elle y était groupée. Désormais opt-in via `Vue > Aide camera`
		// et flotte (pas de DockBuilder associé).
		if (m_showCameraHelp)
		{
		ImGui::Begin("Camera (aide)", &m_showCameraHelp,
			ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoBackground);
		ImGui::TextUnformatted("Vue 3D Vulkan (meme moteur que le jeu).");
		ImGui::TextUnformatted(
			"Deplacement : menu 'Options' -> QWERTY (WASD) ou AZERTY (ZQSD), un seul a la fois. Shift = plus rapide ; "
			"vitesse de base augmente avec la taille du terrain charge.");
		ImGui::TextUnformatted(
			"Orientation : maintenez le clic droit et deplacez la souris (meme au-dessus des fenetres ImGui) ; "
			"sinon la souris n'oriente que lorsqu'ImGui ne la capture pas. Molette : zoom FOV.");
		ImGui::TextUnformatted(
			"Si la vue monte quand vous baissez la souris : dans user_settings.json ou options client, "
			"activez controls.invert_y.");
		if (viewportOverlay != nullptr && viewportOverlay->viewProjColMajor != nullptr)
		{
			ImGui::Separator();
			ImGui::Text("Camera (monde) : (%.2f, %.2f, %.2f) m", static_cast<double>(viewportOverlay->cameraWorldX),
				static_cast<double>(viewportOverlay->cameraWorldY), static_cast<double>(viewportOverlay->cameraWorldZ));
			ImGui::Text("Orientation : yaw %.1fdeg, pitch %.1fdeg", static_cast<double>(viewportOverlay->cameraYawDeg),
				static_cast<double>(viewportOverlay->cameraPitchDeg));
			ImGui::TextDisabled(
				"La grille et le rendu 3D utilisent la meme matrice vue x projection chaque frame : ce n'est pas une grille 'figee' "
				"qui oublierait de se rafraichir. Si ces nombres ne bougent pas avec WASD / souris, la camera ne recoit pas l'entree "
				"(focus ImGui, etc.). S'ils bougent mais l'image ne change pas, il s'agit d'un autre probleme de rendu.");
		}
		ImGui::End();
		} // if (m_showCameraHelp)

		if (m_session && m_cfg)
		{
			if (!m_session->AvailableMapsScanned())
			{
				m_session->RefreshAvailableMaps(*m_cfg);
			}

			ImGui::Begin("Carte");

			// Section 1 - Cartes existantes (chemin canonique world_editor/maps/<zone_id>/).
			ImGui::TextUnformatted("Cartes disponibles");
			if (ImGui::Button("Rafraichir la liste"))
			{
				m_session->RefreshAvailableMaps(*m_cfg);
			}
			const std::vector<std::string>& mapIds = m_session->AvailableMapIds();
			if (mapIds.empty())
			{
				ImGui::TextDisabled("Aucune carte trouvee. Creez-en une ci-dessous, ou cliquez sur 'Rafraichir la liste'.");
			}
			else
			{
				std::string itemsZ;
				for (const std::string& id : mapIds)
				{
					itemsZ += id;
					itemsZ.push_back('\0');
				}
				itemsZ.push_back('\0');
				int& sel = m_session->SelectedAvailableMapIndex();
				sel = std::clamp(sel, 0, static_cast<int>(mapIds.size()) - 1);
				ImGui::Combo("Carte", &sel, itemsZ.c_str());
				sel = std::clamp(sel, 0, static_cast<int>(mapIds.size()) - 1);
				if (ImGui::Button("Charger la carte selectionnee"))
				{
					(void)m_session->ActionLoadMapByZoneId(*m_cfg, mapIds[static_cast<size_t>(sel)]);
				}
			}
			ImGui::Separator();

			// Section 2 - Sauvegarde 1-clic dans le chemin canonique de la carte courante.
			ImGui::TextUnformatted("Carte courante");
			ImGui::InputText("Nom (zone_id)", m_session->BufZoneId().data(), m_session->BufZoneId().size());
			if (ImGui::Button("Sauvegarder"))
			{
				(void)m_session->ActionSaveCurrentMap(*m_cfg);
			}
			ImGui::SameLine();
			// Lot C vague 4 — Bouton « Valider la zone » dans le panneau Carte.
			if (m_shell != nullptr && ImGui::Button("Valider la zone"))
			{
				RunZoneValidation();
			}
			ImGui::SameLine();
			// Lot C vague 4 — Export runtime bloqué si erreurs bloquantes.
			const bool exportBlockedMap = m_validationHasRun && m_lastValidationReport.HasBlockingErrors();
			if (exportBlockedMap)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Exporter runtime"))
			{
				(void)m_session->ActionExportRuntime(*m_cfg);
			}
			if (exportBlockedMap)
			{
				ImGui::EndDisabled();
				ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f),
					"Export bloque : corrigez les erreurs (panneau Validation).");
			}
			if (!m_session->EditFileAbsolutePath().empty())
			{
				ImGui::TextDisabled("Fichier: %s", m_session->EditFileAbsolutePath().c_str());
			}
			ImGui::Separator();

			// Section 3 - Creation d'une nouvelle carte.
			ImGui::TextUnformatted("Nouvelle carte");
			ImGui::InputText("Taille (NxN)", m_session->BufSize().data(), m_session->BufSize().size());
			ImGui::InputTextWithHint("Seed (optionnel)", "vide = aleatoire non fixe",
				m_session->BufSeed().data(), m_session->BufSeed().size());
			if (ImGui::Button("Creer une nouvelle carte"))
			{
				if (m_session->ActionNewMap(*m_cfg))
				{
					m_session->RefreshAvailableMaps(*m_cfg);
				}
			}
			ImGui::Separator();

			// Section 4 - Details fichiers + recharge terrain GPU (avance, replie par defaut).
			if (ImGui::CollapsingHeader("Details fichiers (avance)"))
			{
				ImGui::TextUnformatted("Heightmap (relatif content):");
				ImGui::TextWrapped("%s", m_session->Doc().heightmapContentRelativePath.c_str());
				ImGui::TextUnformatted("Splatmap SLAP (relatif content):");
				ImGui::TextWrapped("%s", m_session->Doc().splatmapContentRelativePath.c_str());
				ImGui::TextUnformatted("Masque herbe GRMS (relatif content):");
				ImGui::TextWrapped("%s", m_session->Doc().grassMaskContentRelativePath.c_str());
				if (ImGui::Button("Recharger terrain GPU"))
				{
					m_session->RequestTerrainGpuReload();
				}
				ImGui::TextDisabled(
					"Chemins canoniques : <content>/world_editor/maps/<zone_id>/{map.lcdlln_edit.json, height.r16h, splat.slap, grass.grms}.");
			}

			ImGui::End();

			ImGui::Begin("Affichage & grille");
			ImGui::Checkbox("Grille (apercu ecran)", &m_session->ShowGrid());
			ImGui::SliderFloat("Maille grille (m)", &m_session->GridCellMeters(), 1.f, 128.f, "%.1f");
			if (viewportOverlay && viewportOverlay->heightmap && viewportOverlay->terrainWorldSize > 0.f)
			{
				const float ws = viewportOverlay->terrainWorldSize;
				const float cellUi = std::max(0.5f, m_session->GridCellMeters());
				const int desiredLines = static_cast<int>(std::ceil(ws / cellUi)) + 1;
				if (desiredLines > kWorldEditorGridMaxLinesPerAxis)
				{
					const float minSpacing =
						ws / static_cast<float>(std::max(1, kWorldEditorGridMaxLinesPerAxis - 1));
					ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f),
						"La grille est limitee a %d lignes par axe (performances). Avec un terrain de %.0f m, "
						"la maille affichee ne peut pas etre plus fine qu'environ %.1f m tant que ce plafond s'applique.",
						kWorldEditorGridMaxLinesPerAxis, static_cast<double>(ws), static_cast<double>(minSpacing));
				}
			}
			ImGui::TextUnformatted("La grille est dessinee en surimpression (projection camera) lorsque le terrain GPU est charge.");
			ImGui::End();

			// ── Panneau Atmosphere ─────────────────────────────────────────────────
			// Permet a l'utilisateur de modifier en direct le cycle jour/nuit :
			// time-of-day (0-24h), timeScale (vitesse du temps), et lecture des
			// valeurs derivees (direction soleil, couleurs ciel zenith/horizon,
			// ambient, isDaytime).
			// Panneau pilote par `m_showAtmospherePanel` (menu `Vue >
			// Atmosphere`). Sans ce p_open, l'utilisateur ne pouvait pas
			// rouvrir le panneau apres l'avoir ferme via la croix du dock.
			if (m_showAtmospherePanel)
			{
			ImGui::Begin("Atmosphere", &m_showAtmospherePanel);
			if (m_dayNight == nullptr)
			{
				ImGui::TextDisabled("DayNightCycle non branche.");
				ImGui::TextDisabled("(SetDayNightCycle pas appele depuis Engine)");
			}
			else
			{
				const engine::render::DayNightCycle::State& dn = m_dayNight->GetState();
				float tod = dn.timeOfDay;
				if (ImGui::SliderFloat("Heure (0-24)", &tod, 0.0f, 24.0f, "%.2f h"))
				{
					m_dayNight->SetTime(tod);
				}
				float ts = m_dayNight->GetTimeScale();
				if (ImGui::SliderFloat("Vitesse temps (s/heure)", &ts, 0.1f, 600.0f, "%.1f"))
				{
					m_dayNight->SetTimeScale(ts);
				}
				ImGui::TextDisabled("60 = 1 min reel = 1 heure jeu (24 min reel = 1 jour jeu)");
				ImGui::TextDisabled("3600 = temps reel 1:1 (24 h reel = 1 jour jeu)");
				ImGui::Separator();
				ImGui::TextUnformatted("Etat calcule (lecture seule) :");
				ImGui::Text("Jour : %s", dn.isDaytime ? "oui (soleil)" : "non (lune)");
				ImGui::Text("Direction soleil : (%.2f, %.2f, %.2f)", dn.lightDir[0], dn.lightDir[1], dn.lightDir[2]);
				const float lightCol[3] = { dn.lightColor[0], dn.lightColor[1], dn.lightColor[2] };
				ImGui::ColorButton("##lightCol", ImVec4(lightCol[0], lightCol[1], lightCol[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Couleur lumiere");
				const float ambCol[3] = { dn.ambientColor[0], dn.ambientColor[1], dn.ambientColor[2] };
				ImGui::ColorButton("##ambCol", ImVec4(ambCol[0], ambCol[1], ambCol[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Couleur ambiente");
				const float zen[3] = { dn.skyZenith[0], dn.skyZenith[1], dn.skyZenith[2] };
				ImGui::ColorButton("##skyZen", ImVec4(zen[0], zen[1], zen[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Ciel (zenith)");
				const float hor[3] = { dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2] };
				ImGui::ColorButton("##skyHor", ImVec4(hor[0], hor[1], hor[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Ciel (horizon)");
				ImGui::TextDisabled("La couleur de fond du viewport prend skyHorizon a chaque frame.");
				ImGui::Separator();
				// Bouton de secours : reset de la camera au-dessus du terrain courant
				// (utile quand l'utilisateur signale "je ne vois plus le terrain"). On
				// passe par le flag de session existant qui declenche RebuildTerrain
				// + reset de la camera (cf. Engine.cpp::RebuildWorldEditorTerrainGpu).
				if (m_session != nullptr && ImGui::Button("Recentrer la camera sur le terrain"))
				{
					m_session->RequestTerrainGpuReload();
				}
				ImGui::Separator();
				// C5 - sauvegarde / chargement de l'atmosphere par zone.
				// Boutons explicites : "Charger depuis carte" applique le timeOfDay+timeScale
				// stockes dans la carte courante au DayNightCycle. "Sauver dans carte"
				// capture les valeurs courantes du cycle vers le document de la carte
				// (le save disque reel se fait via le panneau Carte > Sauvegarder).
				if (m_session != nullptr)
				{
					ImGui::TextUnformatted("Atmosphere par zone :");
					if (m_session->Doc().hasAtmosphere)
					{
						ImGui::Text("Sauvegarde dans carte : %.2fh @ %.0fs/h",
							m_session->Doc().timeOfDayHours, m_session->Doc().timeScale);
					}
					else
					{
						ImGui::TextDisabled("Aucune atmosphere sauvegardee dans la carte courante.");
					}
					if (ImGui::Button("Charger depuis carte"))
					{
						if (m_session->Doc().hasAtmosphere)
						{
							m_dayNight->SetTime(static_cast<float>(m_session->Doc().timeOfDayHours));
							m_dayNight->SetTimeScale(static_cast<float>(m_session->Doc().timeScale));
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Sauver dans carte"))
					{
						auto& docRef = m_session->MutableDoc();
						docRef.hasAtmosphere = true;
						docRef.timeOfDayHours = static_cast<double>(dn.timeOfDay);
						docRef.timeScale = static_cast<double>(m_dayNight->GetTimeScale());
					}
					ImGui::TextDisabled("(Pour persister sur disque, panneau Carte > Sauvegarder)");
				}
			}
			ImGui::End();
			} // if (m_showAtmospherePanel)

			ImGui::Begin("Outils");
			// Etat du terrain - visible en permanence, pour expliquer pourquoi le clic peut etre inactif.
			{
				const bool terrainReady = !m_session->Doc().heightmapContentRelativePath.empty();
				if (terrainReady)
				{
					ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.55f, 1.f), "Terrain : pret");
				}
				else
				{
					ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1.f),
						"Terrain : aucun. Creez ou chargez une carte avant de peindre / sculpter.");
				}
				ImGui::TextDisabled("Le clic gauche est ignore quand la souris est au-dessus de l'UI ; visez la zone 3D.");
				ImGui::Separator();
			}
			// M100.35 — La BeginTabBar("OutilsTabs") historique a été
			// supprimée. Sélection d'outil et paramètres se font désormais
			// via la barre à icônes du WorldEditorShell (en haut du
			// viewport) et le panneau "Tool Properties" du shell. Les
			// sliders/options de l'outil actif (Sculpt, Stamp, Splat, Lake,
			// River, Mountain Range, Valley Chain) y sont rendus
			// contextuellement.
			ImGui::TextDisabled("Sélection d'outil : panneau « Palette d'outils » (à gauche),");
			ImGui::TextDisabled("menu « Outils », ou raccourcis (Ctrl+Shift+...).");
			ImGui::TextDisabled("Paramètres : panneau « Tool Properties ».");
			ImGui::Separator();
			ImGui::TextWrapped(
				"Outils historiquement intégrés ici (« Herbe », « Objets », "
				"« Routes ») seront migrés vers de nouveaux outils dédiés dans "
				"des tickets ultérieurs (M100.18 végétation, M100.17 placement, "
				"M100.29 splines).");
#if 0
			// Bloc historique préservé sous #if 0 pour faciliter la migration
			// progressive des sous-modes vers les nouveaux outils. Ne pas
			// activer : il pousse `m_session->TerrainEditMode()` qui n'a plus
			// de consommateur depuis le passage à `WorldEditorShell::SetActiveTool`.
			if (ImGui::BeginTabBar("OutilsTabs"))
			{
				int& tm = m_session->TerrainEditMode();

				if (ImGui::BeginTabItem("Sculpter"))
				{
					tm = 0;
					ImGui::TextDisabled("Modifie la hauteur du sol au pinceau.");
					const char* ops[] = { "Monter", "Descendre", "Adoucir", "Niveler" };
					ImGui::Combo("Action", &m_session->BrushOp(), ops, IM_ARRAYSIZE(ops));
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche sur le sol pour appliquer.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Peindre"))
				{
					tm = 1;
					ImGui::TextDisabled("Peint le type de sol (herbe, terre, roc, neige).");
					const char* layers[] = { "Herbe", "Terre", "Roc", "Neige" };
					ImGui::Combo("Type de sol", &m_session->SplatLayer(), layers, IM_ARRAYSIZE(layers));
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");

					ImGui::Separator();
					if (ImGui::CollapsingHeader("Textures personnalisees (par couche)"))
					{
						ImGui::TextDisabled("Associez une texture importee a chaque type de sol.");
						const std::vector<std::string>& imported = m_session->Doc().textureAssets;
						std::array<std::string, 4>& refs = m_session->MutableDoc().splatLayerTextureRefs;
						std::string itemsZ;
						itemsZ += "(par defaut moteur)";
						itemsZ.push_back('\0');
						for (const std::string& t : imported)
						{
							itemsZ += t;
							itemsZ.push_back('\0');
						}
						itemsZ.push_back('\0');
						for (int li = 0; li < 4; ++li)
						{
							ImGui::PushID(li);

							// Vignette 48x48 a gauche du combo. Procedurale si
							// ref vide, .texr resamplee sinon. Cellule grise
							// si cache non pret ou decode echoue.
							ImTextureID thumb = 0;
							if (m_texturePreviewCache != nullptr)
							{
								if (refs[static_cast<size_t>(li)].empty())
								{
									thumb = m_texturePreviewCache->GetProceduralThumb(static_cast<uint32_t>(li));
								}
								else
								{
									thumb = m_texturePreviewCache->GetTexrThumb(refs[static_cast<size_t>(li)]);
								}
							}
							if (thumb != 0)
							{
								ImGui::Image(thumb, ImVec2(48.0f, 48.0f));
							}
							else
							{
								ImGui::Dummy(ImVec2(48.0f, 48.0f));
							}
							ImGui::SameLine();

							int sel = 0;
							if (!refs[static_cast<size_t>(li)].empty())
							{
								for (size_t i = 0; i < imported.size(); ++i)
								{
									if (imported[i] == refs[static_cast<size_t>(li)])
									{
										sel = static_cast<int>(i + 1);
										break;
									}
								}
							}
							char lbl[32];
							std::snprintf(lbl, sizeof(lbl), "%s##splatTex%d", layers[li], li);
							if (ImGui::Combo(lbl, &sel, itemsZ.c_str()))
							{
								if (sel <= 0)
								{
									refs[static_cast<size_t>(li)].clear();
								}
								else if (static_cast<size_t>(sel - 1) < imported.size())
								{
									refs[static_cast<size_t>(li)] = imported[static_cast<size_t>(sel - 1)];
								}
								m_session->MarkSplatRefsDirty();  // declenche reupload GPU
							}

							ImGui::PopID();
						}
						ImGui::TextDisabled("Le mapping est persiste dans la carte (champ JSON splat_layer_texture_refs).");
					}

					if (ImGui::CollapsingHeader("Sons de pas (par couche)"))
					{
						ImGui::TextDisabled("Choisissez le son entendu par le joueur sur chaque type de sol.");
						const std::vector<std::string>& imported = m_session->Doc().audioAssets;
						std::array<std::string, 4>& refs = m_session->MutableDoc().splatLayerFootstepAudioRefs;
						std::string itemsZ;
						itemsZ += "(aucun)";
						itemsZ.push_back('\0');
						for (const std::string& t : imported)
						{
							itemsZ += t;
							itemsZ.push_back('\0');
						}
						itemsZ.push_back('\0');
						if (imported.empty())
						{
							ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f),
								"Importez d'abord des sons via le panneau 'Import assets'.");
						}
						for (int li = 0; li < 4; ++li)
						{
							int sel = 0;
							if (!refs[static_cast<size_t>(li)].empty())
							{
								for (size_t i = 0; i < imported.size(); ++i)
								{
									if (imported[i] == refs[static_cast<size_t>(li)])
									{
										sel = static_cast<int>(i + 1);
										break;
									}
								}
							}
							char lbl[40];
							std::snprintf(lbl, sizeof(lbl), "%s##splatAudio%d", layers[li], li);
							if (ImGui::Combo(lbl, &sel, itemsZ.c_str()))
							{
								if (sel <= 0)
								{
									refs[static_cast<size_t>(li)].clear();
								}
								else if (static_cast<size_t>(sel - 1) < imported.size())
								{
									refs[static_cast<size_t>(li)] = imported[static_cast<size_t>(sel - 1)];
								}
							}
						}
						ImGui::TextDisabled(
							"Persiste en JSON. Lecture cote gameplay (deplacement joueur -> couche splat dominante)"
							" sera branchee dans une iteration moteur ulterieure.");
					}

					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche sur le sol pour peindre. La sauvegarde ecrit le fichier splat.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Herbe"))
				{
					tm = 2;
					ImGui::TextDisabled("Definit ou des touffes d'herbe poussent.");
					ImGui::Checkbox("Mode gomme (efface l'herbe)", &m_session->GrassMaskEraseBrush());
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche pour appliquer. La sauvegarde ecrit grass.grms.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Objets"))
				{
					tm = 3;
					ImGui::TextDisabled("Pose des arbres, rochers ou autres objets.");
					ImGui::TextWrapped(
						"Choisissez le type d'objet dans le panneau 'Objets sur la carte' a droite.");
					ImGui::Separator();
					ImGui::TextWrapped(
						"Clic gauche sur le sol : pose un nouvel objet, ou deplace l'objet selectionne dans la liste.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Eau"))
				{
					// Pas un mode de pinceau : on ne change pas tm.
					ImGui::TextDisabled("Active une surface d'eau plane a un Y donne (lac, mer).");
					bool waterOn = m_session->Doc().waterEnabled;
					if (ImGui::Checkbox("Eau active", &waterOn))
					{
						m_session->MutableDoc().waterEnabled = waterOn;
					}
					float lvl = static_cast<float>(m_session->Doc().waterLevelMeters);
					if (ImGui::SliderFloat("Niveau de l'eau (Y, m)", &lvl, -200.f, 500.f, "%.2f"))
					{
						m_session->MutableDoc().waterLevelMeters = static_cast<double>(lvl);
					}
					ImGui::Separator();
					ImGui::TextDisabled(
						"Ces reglages sont persistes dans la carte (champs JSON water_enabled,");
					ImGui::TextDisabled(
						"water_level_m). La passe Vulkan eau (surface transparente, reflets simples)");
					ImGui::TextDisabled(
						"sera branchee dans une iteration moteur ulterieure.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Routes"))
				{
					tm = 4;
					ImGui::TextDisabled("Trace des chemins / routes en peignant une bande de splat.");
					const char* rlayers[] = { "Herbe", "Terre / chemin", "Roc", "Neige" };
					ImGui::Combo("Sol de la route", &m_session->RouteSplatLayer(), rlayers, IM_ARRAYSIZE(rlayers));
					{
						int& rl = m_session->RouteSplatLayer();
						rl = std::clamp(rl, 0, 3);
					}
					ImGui::SliderFloat("Largeur (m)", &m_session->RouteStripWidthM(), 0.5f, 64.f, "%.1f");
					ImGui::SliderFloat("Intensite", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					if (ImGui::Button("Effacer les points"))
					{
						m_session->ClearRouteDraft();
					}
					ImGui::SameLine();
					if (ImGui::Button("Tracer la route"))
					{
						m_session->RequestApplyRouteDraftToSplat();
					}
					ImGui::Text("Points places : %zu", m_session->RouteDraftPoints().size());
					ImGui::Text("Routes enregistrees : %zu", m_session->Doc().routes.size());
					ImGui::Separator();
					ImGui::TextWrapped("Cliquez sur le sol pour ajouter un sommet de la route, puis 'Tracer la route'.");
					ImGui::EndTabItem();
				}

				tm = std::clamp(tm, 0, 4);
				ImGui::EndTabBar();
			}
#endif
			ImGui::End();

			ImGui::Begin("Import assets");
			ImGui::TextUnformatted("Texture (PNG / JPG / TGA / BMP)");
			ImGui::InputTextWithHint("Source", "C:/chemin/vers/image.png (guillemets autorises)",
				m_session->BufPngPath().data(), m_session->BufPngPath().size());
			ImGui::InputTextWithHint("Nom dans textures/", "vide = deduit automatiquement (ex: image.texr)",
				m_session->BufTexrName().data(), m_session->BufTexrName().size());
			if (ImGui::Button("Importer cette texture"))
			{
				(void)m_session->ActionImportTexture(*m_cfg);
			}
			ImGui::TextDisabled("L'extension .texr est ajoutee si absente. Le fichier est ecrit dans <content>/textures/.");
			ImGui::Separator();

			ImGui::TextUnformatted("Audio (WAV / OGG)");
			ImGui::InputTextWithHint("Source##audio", "C:/chemin/vers/son.wav (guillemets autorises)",
				m_session->BufAudioSrc().data(), m_session->BufAudioSrc().size());
			ImGui::InputTextWithHint("Nom dans audio/", "vide = meme nom que la source (ex: footstep/sand.wav)",
				m_session->BufAudioDest().data(), m_session->BufAudioDest().size());
			if (ImGui::Button("Importer cet audio"))
			{
				(void)m_session->ActionImportAudio(*m_cfg);
			}
			ImGui::TextDisabled("Le fichier est copie dans <content>/audio/. Aucune transcompression.");
			ImGui::Separator();

			ImGui::TextUnformatted("Textures deja importees sur cette carte:");
			if (m_session->Doc().textureAssets.empty())
			{
				ImGui::TextDisabled("(aucune)");
			}
			else
			{
				for (const std::string& t : m_session->Doc().textureAssets)
				{
					ImGui::BulletText("%s", t.c_str());
				}
			}
			ImGui::End();

			ImGui::Begin("Objets sur la carte");
			if (m_cfg != nullptr)
			{
				m_session->EnsureTreeCatalogLoaded(*m_cfg);
			}
			const char* placeKinds[] = { "Arbre (catalogue 013)", "Rocher (legacy zone_1)" };
			{
				int& pk = m_session->InstancePlacementKind();
				ImGui::Combo("Type a placer", &pk, placeKinds, IM_ARRAYSIZE(placeKinds));
				pk = std::clamp(pk, 0, 1);
			}
			if (m_session->InstancePlacementKind() == 0)
			{
				const std::vector<TreeSpeciesSpec>& specs = m_session->TreeCatalog().Species();
				if (specs.empty())
				{
					ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
						"Catalogue arbres vide ou invalide (voir world_editor/tree_species_catalog.json).");
				}
				else
				{
					std::string speciesItems;
					for (const TreeSpeciesSpec& s : specs)
					{
						speciesItems += s.id;
						speciesItems.push_back('\0');
					}
					speciesItems.push_back('\0');
					int& si = m_session->TreeSpeciesUiIndex();
					const int prevSi = si;
					si = std::clamp(si, 0, static_cast<int>(specs.size()) - 1);
					ImGui::Combo("Espece", &si, speciesItems.c_str());
					si = std::clamp(si, 0, static_cast<int>(specs.size()) - 1);
					if (si != prevSi)
					{
						m_session->TreeShapeVariantUiIndex() = 0;
					}
					const TreeSpeciesSpec& sp = specs[static_cast<size_t>(si)];
					std::string shapeItems;
					for (const TreeSpeciesShapeSpec& sh : sp.shapes)
					{
						std::string lab = sh.gltfContentRelativePath;
						const size_t slash = lab.find_last_of("/\\");
						if (slash != std::string::npos)
						{
							lab = lab.substr(slash + 1);
						}
						shapeItems += lab;
						shapeItems.push_back('\0');
					}
					shapeItems.push_back('\0');
					int& shi = m_session->TreeShapeVariantUiIndex();
					shi = std::clamp(shi, 0, static_cast<int>(sp.shapes.size()) - 1);
					ImGui::Combo("Forme (variante glTF)", &shi, shapeItems.c_str());
					shi = std::clamp(shi, 0, static_cast<int>(sp.shapes.size()) - 1);
					ImGui::SliderFloat("Taille (min-max espece)", &m_session->TreeScaleT01(), 0.f, 1.f, "%.2f");
					ImGui::Checkbox("Echelle aleatoire a la pose", &m_session->TreeRandomizeScaleOnPlace());
					if (m_session->TreeRandomizeScaleOnPlace())
					{
						ImGui::TextDisabled("Le curseur taille est ignore si aleatoire est coche.");
					}
				}
			}
			else
			{
				ImGui::TextUnformatted("Pose un rocher (zones/zone_1/zone_1.gltf), sans espece catalogue.");
			}
			ImGui::TextUnformatted(
				"Selectionnez une ligne pour deplacer au prochain clic. Sans selection : nouvelle instance. Coordonnees monde + snap hauteur MNT.");
			ImGui::Separator();
			for (size_t i = 0; i < m_session->MutableDoc().layoutInstances.size(); ++i)
			{
				const engine::editor::WorldMapEditLayoutInstance& inst = m_session->MutableDoc().layoutInstances[i];
				char label[256];
				if (!inst.speciesId.empty())
				{
					std::snprintf(label, sizeof(label), "%s - %s #%u - scale %.3f##instrow%zu", inst.guid.c_str(), inst.speciesId.c_str(),
						static_cast<unsigned>(inst.shapeVariantIndex), inst.uniformScale, i);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%s - %s##instrow%zu", inst.guid.c_str(), inst.gltfContentRelativePath.c_str(), i);
				}
				const bool sel = (m_session->SelectedLayoutInstanceIndex() == static_cast<int>(i));
				if (ImGui::Selectable(label, sel))
				{
					m_session->SelectedLayoutInstanceIndex() = static_cast<int>(i);
				}
				ImGui::SameLine();
				char delId[48];
				std::snprintf(delId, sizeof(delId), "Suppr##%zu", i);
				if (ImGui::SmallButton(delId))
				{
					m_session->RemoveLayoutInstance(i);
					break;
				}
			}
			{
				const int sel = m_session->SelectedLayoutInstanceIndex();
				if (sel >= 0 && static_cast<size_t>(sel) < m_session->MutableDoc().layoutInstances.size())
				{
					engine::editor::WorldMapEditLayoutInstance& inst = m_session->MutableDoc().layoutInstances[static_cast<size_t>(sel)];
					if (!inst.speciesId.empty())
					{
						const TreeSpeciesSpec* spec = m_session->TreeCatalog().FindById(inst.speciesId);
						if (spec != nullptr && !spec->shapes.empty())
						{
							ImGui::Separator();
							ImGui::TextUnformatted("Edition instance selectionnee (arbre)");
							std::string shapeItemsSel;
							for (const TreeSpeciesShapeSpec& sh : spec->shapes)
							{
								std::string lab = sh.gltfContentRelativePath;
								const size_t slash = lab.find_last_of("/\\");
								if (slash != std::string::npos)
								{
									lab = lab.substr(slash + 1);
								}
								shapeItemsSel += lab;
								shapeItemsSel.push_back('\0');
							}
							shapeItemsSel.push_back('\0');
							int sh = static_cast<int>(inst.shapeVariantIndex);
							sh = std::clamp(sh, 0, static_cast<int>(spec->shapes.size()) - 1);
							(void)ImGui::Combo("Forme##edit_sel", &sh, shapeItemsSel.c_str());
							sh = std::clamp(sh, 0, static_cast<int>(spec->shapes.size()) - 1);
							inst.shapeVariantIndex = static_cast<uint32_t>(sh);
							inst.gltfContentRelativePath = spec->shapes[static_cast<size_t>(sh)].gltfContentRelativePath;
							float scf = static_cast<float>(inst.uniformScale);
							(void)ImGui::SliderFloat("Echelle##edit_sel", &scf, static_cast<float>(spec->scaleMin), static_cast<float>(spec->scaleMax),
								"%.3f");
							inst.uniformScale = static_cast<double>(scf);
						}
					}
				}
			}
			if (ImGui::Button("Aucune selection (pose toujours une nouvelle instance)"))
			{
				m_session->SelectedLayoutInstanceIndex() = -1;
			}
			ImGui::End();

			// Réorganisation UI 2026-07-17 — barre de statut enrichie
			// (convention UE) : message de session | carte courante | outil
			// actif, et à droite l'indicateur « Tout enregistré / Non
			// sauvegardé » (même source dirty que la modale Quitter :
			// WorldEditorShell::IsDirtySinceSave). Remplace l'affichage du
			// statut dans la barre de menu (supprimé par la réorganisation).
			ImGui::Begin("Statut", nullptr, ImGuiWindowFlags_NoMouseInputs);
			{
				ImGui::TextUnformatted(m_session->Status().c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("|  Carte : %s", m_session->Doc().zoneId.c_str());
				if (m_shell != nullptr)
				{
					ImGui::SameLine();
					const engine::editor::world::ToolIconStyle toolStyle =
						engine::editor::world::ToolbarIconAtlas::Get(m_shell->GetActiveTool());
					ImGui::TextDisabled("|  Outil : %s",
						(m_shell->GetActiveTool() == engine::editor::world::ActiveTool::None)
							? "aucun" : toolStyle.tooltipFr);

					// Indicateur de sauvegarde aligné à droite.
					const bool dirty = m_shell->IsDirtySinceSave();
					const char* saveText =
						dirty ? "\xE2\x97\x8F Non sauvegardé" : "Tout enregistré";
					const float textW = ImGui::CalcTextSize(saveText).x;
					const float avail = ImGui::GetContentRegionAvail().x;
					if (avail > textW + 16.0f)
					{
						ImGui::SameLine(0.0f, avail - textW - 8.0f);
					}
					else
					{
						ImGui::SameLine();
					}
					if (dirty)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.77f, 0.25f, 1.0f), "%s", saveText);
					}
					else
					{
						ImGui::TextDisabled("%s", saveText);
					}
				}
			}
			ImGui::End();
		}
		else
		{
			ImGui::Begin("Proprietes", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextUnformatted("Session editeur non initialisee.");
			ImGui::End();
		}

		if (viewportOverlay)
		{
			DrawViewportOverlaysImpl(*viewportOverlay);
		}

		// Panneau Bibliotheque de textures (toggle via menu Vue).
		if (m_showTextureLibrary && m_session != nullptr)
		{
			engine::editor::DrawTextureLibrary(*m_session, m_texturePreviewCache, m_showTextureLibrary);
		}

		// Lot C vague 4 — Panneau Validation (rapport trié par sévérité) + voile
		// de guidance overlay (no-op tant qu'aucune séquence de tutoriel n'est
		// active). Dessinés en fin de frame : l'overlay est posé tout en haut via
		// le foreground draw list, par-dessus tous les panneaux ci-dessus.
		RenderValidationPanel();
		// Lot C vague 4 — Panneau Diagnostic (« Pourquoi ça ne marche pas ? »).
		// Rendu avant l'overlay de guidance pour que ce dernier (foreground draw
		// list) puisse, le cas échéant, le surligner comme cible de tutoriel.
		RenderDiagnosticPanel();
		// Lot C vague 4 — Fenêtre de l'assistant « Nouvelle zone » (no-op si
		// fermée). Rendue avant l'overlay de guidance pour que ce dernier puisse,
		// le cas échéant, surligner ses widgets comme cibles de tutoriel.
		RenderWizardWindow();
		RenderGuidanceOverlay();

		ImGui::Render();
#else
		(void)viewportOverlay;
#endif
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Lot C vague 4 — Validation de zone (ZoneValidator) + guidance overlay.
	// ─────────────────────────────────────────────────────────────────────────

	void WorldEditorImGui::RunZoneValidation()
	{
		// Sans shell branché, aucun document à valider ; sans config, les chunks
		// terrain ne peuvent pas être chargés depuis le disque (EnsureLoaded en a
		// besoin). Dans ces deux cas, on produit un rapport vide marqué « lancé ».
		if (m_shell == nullptr || m_cfg == nullptr)
		{
			m_lastValidationReport = engine::editor::world::validation::ZoneValidator::Report{};
			m_validationHasRun = true;
			m_showValidationPanel = true;
			LOG_WARN(Render,
				"[WorldEditorImGui] Validation impossible : shell ou config non branche.");
			return;
		}

		namespace val = engine::editor::world::validation;
		namespace terr = engine::world::terrain;

		// Adaptateur documents -> ValidationContext (vues lecture seule). Les
		// chunks terrain proviennent du TerrainDocument (source de vérité de la
		// zone) : on itère ceux résidents en RAM via ForEachLoadedChunk et on
		// situe chacun par l'origine monde de son coin (coord.x/z * largeur chunk).
		// La largeur d'un chunk est (resolution-1) cellules = 256 m (kTerrain*).
		val::ValidationContext ctx;

		auto& terrainDoc = m_shell->MutableTerrainDocument();
		const float chunkSpanMeters =
			static_cast<float>(terr::kTerrainResolution - 1u) * terr::kTerrainCellSizeMeters;
		// Mémorise la coord du 1er chunk chargé pour relier sa splat ensuite.
		bool hasFirstCoord = false;
		engine::world::GlobalChunkCoord firstCoord{};
		engine::math::Vec3 firstOriginWorld{ 0.0f, 0.0f, 0.0f };
		terrainDoc.ForEachLoadedChunk(
			[&](engine::world::GlobalChunkCoord coord,
				const std::shared_ptr<terr::TerrainChunk>& chunk)
			{
				if (!chunk)
				{
					return;
				}
				val::ValidationContext::TerrainEntry entry;
				entry.chunk = chunk.get();
				entry.originWorld = engine::math::Vec3{
					static_cast<float>(coord.x) * chunkSpanMeters,
					0.0f,
					static_cast<float>(coord.z) * chunkSpanMeters };
				if (!hasFirstCoord)
				{
					hasFirstCoord = true;
					firstCoord = coord;
					firstOriginWorld = entry.originWorld;
				}
				ctx.terrainChunks.push_back(entry);
			});

		// Splat : le TerrainDocument stocke une SplatMap par chunk (pas de splat
		// global unifié). Le ValidationContext n'expose qu'un seul pointeur splat
		// (MVP mono-zone) : on relie la splat du 1er chunk chargé si présente,
		// avec son origine monde. Les chunks sans splat chargée sont ignorés (la
		// règle splat passe simplement). Le scan multi-chunk est différé (2e passe).
		if (hasFirstCoord)
		{
			if (auto splat = terrainDoc.FindSplat(firstCoord))
			{
				ctx.splat = splat.get();
				ctx.splatOriginWorld = firstOriginWorld;
			}
		}

		// Mesh inserts : vue directe sur le vecteur du MeshInsertDocument.
		ctx.meshInserts = &m_shell->GetMeshInsertDocument().All();

		m_lastValidationReport = m_zoneValidator.Validate(ctx);
		m_validationHasRun = true;
		m_showValidationPanel = true;
		LOG_INFO(Render,
			"[WorldEditorImGui] Validation zone : {} erreur(s), {} avertissement(s), {} indice(s) "
			"sur {} chunk(s) terrain, {} mesh insert(s).",
			m_lastValidationReport.errorCount, m_lastValidationReport.warningCount,
			m_lastValidationReport.hintCount, ctx.terrainChunks.size(),
			ctx.meshInserts ? ctx.meshInserts->size() : static_cast<size_t>(0));
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Lot C vague 4 — Diagnostic « Pourquoi ça ne marche pas ? ».
	// BuildDiagnosticContext est CROSS-PLATFORM (hors garde _WIN32, comme
	// RunZoneValidation) : tous les types y sont pleinement qualifiés (le build
	// Linux compile cette fonction).
	// ─────────────────────────────────────────────────────────────────────────

	engine::editor::world::diagnostic::DiagnosticContext
	WorldEditorImGui::BuildDiagnosticContext() const
	{
		namespace diag  = engine::editor::world::diagnostic;
		namespace world = engine::editor::world;

		diag::DiagnosticContext ctx;

		// Sans shell branché : aucun état d'usage à inspecter, on renvoie un
		// contexte vide (la règle « aucun outil sélectionné » se déclenchera, ce
		// qui est le bon message dans cet état).
		if (m_shell == nullptr)
		{
			return ctx;
		}

		// --- Outil actif → bool + identifiant chaîne ---------------------------
		// On mappe l'enum `ActiveTool` du shell vers les ids attendus par les
		// règles (chaînes courtes, ex. "coastline"). `None` => pas d'outil actif.
		const world::ActiveTool tool = m_shell->GetActiveTool();
		ctx.hasActiveTool = (tool != world::ActiveTool::None);
		switch (tool)
		{
		case world::ActiveTool::TerrainSculpt:      ctx.activeToolId = "terrain_sculpt"; break;
		case world::ActiveTool::TerrainStamp:       ctx.activeToolId = "terrain_stamp";  break;
		case world::ActiveTool::SplatPaint:         ctx.activeToolId = "splat_paint";    break;
		case world::ActiveTool::Lake:               ctx.activeToolId = "lake";           break;
		case world::ActiveTool::River:              ctx.activeToolId = "river";          break;
		case world::ActiveTool::MountainRange:      ctx.activeToolId = "mountain_range"; break;
		case world::ActiveTool::ValleyChain:        ctx.activeToolId = "valley_chain";   break;
		case world::ActiveTool::RiverNetwork:       ctx.activeToolId = "river_network";  break;
		case world::ActiveTool::Coastline:          ctx.activeToolId = "coastline";      break;
		case world::ActiveTool::HydraulicErosion:   ctx.activeToolId = "hydraulic";      break;
		case world::ActiveTool::ThermalWindErosion: ctx.activeToolId = "thermal_wind";   break;
		case world::ActiveTool::Cave:               ctx.activeToolId = "cave";           break;
		case world::ActiveTool::Overhang:           ctx.activeToolId = "overhang";       break;
		case world::ActiveTool::Arch:               ctx.activeToolId = "arch";           break;
		case world::ActiveTool::DungeonPortal:      ctx.activeToolId = "dungeon_portal"; break;
		case world::ActiveTool::None:               break; // ctx.activeToolId reste vide
		}
		// Drapeau spécifique consommé par NoSeaLevelSetRule.
		ctx.coastlineToolActive = (tool == world::ActiveTool::Coastline);

		// --- Nombre de chunks chargés ------------------------------------------
		// Source fiable : compteur exposé par le TerrainDocument (chunks résidents
		// en RAM). Lecture seule — pas de chargement disque ici (contrairement à
		// RunZoneValidation), pour qu'un simple « Analyser » reste sans effet de bord.
		ctx.chunkCount = static_cast<uint32_t>(
			m_shell->MutableTerrainDocument().LoadedChunkCount());

		// --- Profondeur undo → proxy de « commandes depuis la dernière save » --
		// Le CommandStack n'expose AUCUN marqueur de sauvegarde (pas de compteur
		// « depuis le dernier save »). On utilise `UndoSize()` comme PROXY : c'est
		// la profondeur totale de la pile undo, pas strictement le nombre de
		// modifications depuis le dernier save (un save ne réinitialise pas la
		// pile). Suffisant pour la règle « modifications non sauvegardées » (seuil
		// > 30) ; un vrai marqueur de save est à instrumenter en 2e passe.
		ctx.commandsSinceLastSave = static_cast<uint32_t>(
			m_shell->GetCommandStack().UndoSize());

		// --- Sea level défini --------------------------------------------------
		// `seaLevelMeters` vaut TOUJOURS 50 par défaut : il n'existe pas d'état
		// « défini vs non défini » sur cette valeur. On utilise `OceanSettings::
		// enabled` comme PROXY de « niveau de mer défini » (océan activé = côte a
		// une référence d'altitude). Heuristique, pas un signal exact — à affiner
		// en 2e passe si un vrai flag « sea level explicitement posé » est ajouté.
		ctx.seaLevelSet = m_shell->GetWaterDocument().GetOcean().enabled;

		// --- Mode éditeur Simple/Avancé ----------------------------------------
		// Source fiable : le singleton EditorModeRegistry (même source que le
		// toggle « Mode editeur » du menu Options). `attemptedAdvancedFeature` n'a
		// en revanche PAS de source de suivi — il reste à son défaut neutre (false),
		// donc SimpleModeAdvancedAttemptedRule ne se déclenchera pas tant que ce
		// signal n'est pas instrumenté (2e passe).
		ctx.simpleModeActive =
			(world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== world::modes::EditorMode::Simple);

		// --- Validation (peuple validationErrorCount depuis le dernier rapport) -
		// On ne relie PAS `ctx.validation` : ce pointeur attend un
		// `ValidationContext` (vues documents), or on ne conserve entre frames que
		// le `ZoneValidator::Report` (`m_lastValidationReport`), pas le contexte
		// source (construit localement et détruit dans RunZoneValidation). On
		// transfère donc seulement le compteur d'erreurs, suffisant pour
		// ExportAttemptedWithErrorsRule.
		if (m_validationHasRun)
		{
			ctx.validationErrorCount = m_lastValidationReport.errorCount;
		}

		// --- Champs SANS source de suivi runtime fiable : défauts neutres ------
		// Les champs ci-dessous n'ont pas de source d'instrumentation à ce stade ;
		// on les laisse à leur valeur par défaut sûre (pas de fausse donnée) — à
		// instrumenter en 2e passe (suivi temporel + journal d'actions d'usage) :
		//   - hasOpenedDialog               : pas de suivi d'ouverture de dialogue.
		//   - secondsSinceToolSelected      : pas d'horodatage de sélection d'outil.
		//   - commandsSinceToolSelected     : pas de compteur ré-armé à la sélection.
		//   - presetJustAppliedNotSaved     : pas de signal « preset appliqué ».
		//   - hasUserAttemptedExport        : pas de trace de tentative d'export.
		//   - erosionAppliedAfterRivers     : pas de suivi d'ordre érosion/rivières.
		//   - cavePlacedWithoutCamouflage   : pas d'analyse jointure grotte/splat.
		//   - attemptedAdvancedFeature      : pas de trace d'accès param avancé.
		//   - recentCommandHistory          : pas d'historique de labels exposé.
		// Conséquence : les règles dépendant uniquement de ces champs restent
		// silencieuses (comportement voulu — mieux que des suggestions fantaisistes).

		return ctx;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Lot C vague 4 — Assistant « Nouvelle zone » (QuickStartWizard).
	// RunWizardGeneration est CROSS-PLATFORM (hors garde _WIN32, comme
	// RunZoneValidation / BuildDiagnosticContext) : tous les types y sont
	// pleinement qualifiés (le build Linux compile cette fonction).
	//
	// NOTE P0 (rollback transactionnel) — partagée avec le ZonePresetDialog :
	// `ZonePresetExecutor::Execute` commence par VIDER les 4 documents de la
	// zone (terrain / water / mesh inserts / portails de donjon) via
	// `ResetEditedZoneDocuments`. Ce reset est DESTRUCTIF et NON annulable (il
	// n'est pas poussé comme commande sur le CommandStack). Si l'exécution
	// échoue en plein milieu, la zone précédente est déjà perdue. Un rollback
	// transactionnel (snapshot avant reset + restauration sur échec/annulation)
	// est un follow-up P0 qui bénéficierait aux DEUX points d'entrée — le dialog
	// preset ET cet assistant — puisqu'ils partagent le même `ZonePresetExecutor`.
	// En attendant, les deux UI affichent une modale de confirmation explicite
	// recommandant de sauvegarder d'abord.
	// ─────────────────────────────────────────────────────────────────────────
	void WorldEditorImGui::RunWizardGeneration()
	{
		// Sans shell branché, aucun document cible : on ne génère rien (le menu
		// est déjà désactivé dans ce cas, ceci est une garde défensive).
		if (m_shell == nullptr)
		{
			LOG_WARN(Render,
				"[WorldEditorImGui] Generation wizard impossible : shell non branche.");
			return;
		}

		namespace zp     = engine::editor::world::zone_presets;
		namespace wizard = engine::editor::world::wizard;

		// 1) Choix du wizard → ZonePreset (résolution déterministe, id dérivé des
		//    choix). Le seed UI est déjà recopié dans le wizard via SetSeed.
		const wizard::WizardChoices& choices = m_wizard.Choices();
		const zp::ZonePreset preset = m_wizardResolver.Resolve(choices);

		// 2) Customisation : le wizard ne porte pas de curseurs relief/eau/dryness
		//    (ils sont encodés dans le template résolu). On laisse les
		//    multiplicateurs neutres (1.0) et on propage le seed du wizard, comme
		//    le ZonePresetDialog le fait pour son propre champ seed.
		zp::CustomizationParams custom;
		custom.reliefMultiplier       = 1.0f;
		custom.waterDensityMultiplier = 1.0f;
		custom.drynessMultiplier      = 1.0f;
		custom.seed                   = choices.seed;

		// 3) DispatchContext construit EXACTEMENT comme dans
		//    ZonePresetDialog::RunSelectedPreset (mêmes documents, mêmes catalogs,
		//    même Config pour les 4 ops simulation). Réutilisation du chemin
		//    canonique : aucune divergence de câblage entre dialog et wizard.
		const zp::DispatchContext ctx{
			m_shell->MutableTerrainDocument(),
			m_shell->MutableWaterDocument(),
			m_shell->MutableMeshInsertDocument(),
			m_shell->MutableDungeonPortalDocument(),
			m_shell->GetCaveTool().Catalog(),
			m_shell->GetOverhangTool().Catalog(),
			m_shell->GetArchTool().Catalog(),
			m_shell->GetDungeonPortalTool().Catalog(),
			m_cfg,  // requis par les 4 ops simulation ; si null elles renvoient Failed.
		};

		LOG_INFO(Render,
			"[WorldEditorImGui] Wizard genere '{}' (climat={} relief={} cote={} poi={} seed={})",
			preset.id, choices.climate, choices.relief, choices.coast, choices.poi,
			choices.seed);

		// 4) Exécution synchrone sur le CommandStack du Shell (main thread bloqué
		//    le temps de l'exécution — convention single-thread éditeur, cf.
		//    ZonePresetExecutor.h). Callback de progression inerte (on ne peut pas
		//    pumper ImGui pendant Execute) ; la progression part au log.
		zp::ZonePresetExecutor executor;
		m_wizardLastSummary = executor.Execute(preset, custom,
			m_shell->MutableCommandStack(), ctx,
			[](const zp::ExecutionProgress&) { return true; });

		m_wizardLastPresetId = preset.id;
		m_wizardHasGenerated = true;

		LOG_INFO(Render,
			"[WorldEditorImGui] Wizard '{}' termine (pushed={}, skipped={}, failed={}, annule={}).",
			preset.id, m_wizardLastSummary.commandsPushed,
			m_wizardLastSummary.unsupportedSkipped, m_wizardLastSummary.failed,
			m_wizardLastSummary.wasCancelled ? "oui" : "non");
	}

#if defined(_WIN32)
	void WorldEditorImGui::RenderValidationPanel()
	{
		if (!m_showValidationPanel)
		{
			return;
		}
		namespace val = engine::editor::world::validation;

		ImGui::Begin("Validation", &m_showValidationPanel);

		// Bouton de relance directement dans le panneau (en plus du menu/toolbar).
		if (ImGui::Button("Revalider la zone"))
		{
			RunZoneValidation();
		}
		ImGui::SameLine();
		if (!m_validationHasRun)
		{
			ImGui::TextDisabled("Aucune validation lancee.");
			ImGui::End();
			return;
		}

		const val::ZoneValidator::Report& rep = m_lastValidationReport;

		// Compteurs colorés (erreurs rouge / warnings ambre / hints gris).
		ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "Erreurs : %u", rep.errorCount);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "Avertissements : %u", rep.warningCount);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.f), "Indices : %u", rep.hintCount);

		if (rep.HasBlockingErrors())
		{
			ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f),
				"Export runtime BLOQUE tant que des erreurs subsistent.");
		}
		ImGui::Separator();

		if (rep.issues.empty())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.55f, 1.f),
				"Aucun probleme detecte. La zone est exportable.");
			ImGui::End();
			return;
		}

		// Liste triée par sévérité décroissante (garanti par ZoneValidator).
		// Un clic sur un problème logue sa position monde (« Aller a » caméra
		// complet différé : l'API caméra n'est pas exposée à ce niveau).
		for (size_t i = 0; i < rep.issues.size(); ++i)
		{
			const val::ValidationIssue& issue = rep.issues[i];
			ImVec4 sevColor;
			const char* sevLabel;
			switch (issue.severity)
			{
			case val::Severity::Error:   sevColor = ImVec4(1.f, 0.4f, 0.35f, 1.f);  sevLabel = "[ERREUR]";  break;
			case val::Severity::Warning: sevColor = ImVec4(1.f, 0.82f, 0.35f, 1.f); sevLabel = "[ATTENTION]"; break;
			default:                     sevColor = ImVec4(0.7f, 0.7f, 0.7f, 1.f);  sevLabel = "[INDICE]";  break;
			}
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextColored(sevColor, "%s", sevLabel);
			ImGui::SameLine();
			// Selectable cliquable : logue la position pour situer le problème.
			if (ImGui::Selectable(issue.title.empty() ? issue.ruleId.c_str() : issue.title.c_str()))
			{
				LOG_INFO(Render,
					"[WorldEditorImGui] Probleme '{}' (regle {}) a la position monde ({:.1f}, {:.1f}, {:.1f}).",
					issue.title, issue.ruleId,
					issue.worldPosition.x, issue.worldPosition.y, issue.worldPosition.z);
			}
			if (!issue.description.empty())
			{
				ImGui::TextWrapped("%s", issue.description.c_str());
			}
			if (!issue.suggestedFix.empty())
			{
				ImGui::TextDisabled("Correctif suggere : %s", issue.suggestedFix.c_str());
			}
			ImGui::PopID();
			ImGui::Separator();
		}

		ImGui::End();
	}

	void WorldEditorImGui::RenderGuidanceOverlay()
	{
		// Fondation tutoriel : tant qu'aucune séquence n'est lancée, on ne dessine
		// strictement rien (pas de voile, pas de surlignage).
		if (!m_overlay.IsActiveSequence())
		{
			return;
		}

		const engine::editor::world::help::OverlayInstruction& instr =
			m_overlay.CurrentInstruction();

		ImDrawList* fg = ImGui::GetForegroundDrawList();
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 vpMin = vp->Pos;
		const ImVec2 vpMax = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);

		// Voile semi-transparent plein écran (assombrit le reste de l'UI).
		fg->AddRectFilled(vpMin, vpMax, IM_COL32(0, 0, 0, 110));

		// Rectangle de surlignage autour de la cible (si le widget est enregistré
		// cette frame et que son rectangle est valide).
		bool found = false;
		const engine::editor::world::help::WidgetRect rect =
			m_widgetTargets.Get(instr.targetWidget, &found);
		ImVec2 bubbleAnchor = ImVec2(vpMin.x + 40.f, vpMin.y + 80.f);
		if (found && rect.Valid())
		{
			const ImVec2 rMin(rect.x0 - 4.f, rect.y0 - 4.f);
			const ImVec2 rMax(rect.x1 + 4.f, rect.y1 + 4.f);
			// « Trou » lumineux : on redessine la zone cible sans voile en la
			// surlignant d'un cadre épais ambre.
			fg->AddRect(rMin, rMax, IM_COL32(255, 209, 89, 255), 4.f, 0, 3.f);
			bubbleAnchor = ImVec2(rMin.x, rMax.y + 8.f);
		}

		// Bulle titre/texte ancrée sous la cible (ou en haut-gauche par défaut).
		const std::string title = instr.iconHint.empty()
			? instr.titleFr
			: (instr.iconHint + " " + instr.titleFr);
		const float bubbleW = 360.f;
		const ImVec2 pad(10.f, 8.f);
		const float titleH = ImGui::GetTextLineHeight();
		// Hauteur approximative : titre + corps wrappé (estimation simple).
		const ImVec2 bodySize = ImGui::CalcTextSize(
			instr.bodyFr.c_str(), nullptr, false, bubbleW - pad.x * 2.f);
		const ImVec2 bubbleMin = bubbleAnchor;
		const ImVec2 bubbleMax = ImVec2(
			bubbleMin.x + bubbleW,
			bubbleMin.y + titleH + bodySize.y + pad.y * 3.f);
		fg->AddRectFilled(bubbleMin, bubbleMax, IM_COL32(28, 30, 38, 240), 6.f);
		fg->AddRect(bubbleMin, bubbleMax, IM_COL32(255, 209, 89, 255), 6.f, 0, 1.5f);
		fg->AddText(ImVec2(bubbleMin.x + pad.x, bubbleMin.y + pad.y),
			IM_COL32(255, 224, 130, 255), title.c_str());
		fg->AddText(nullptr, 0.f,
			ImVec2(bubbleMin.x + pad.x, bubbleMin.y + pad.y * 2.f + titleH),
			IM_COL32(230, 230, 230, 255),
			instr.bodyFr.c_str(), nullptr, bubbleW - pad.x * 2.f);
	}

	void WorldEditorImGui::RenderDiagnosticPanel()
	{
		if (!m_showDiagnosticPanel)
		{
			return;
		}
		namespace diag = engine::editor::world::diagnostic;

		ImGui::Begin("Diagnostic", &m_showDiagnosticPanel);

		ImGui::TextWrapped(
			"Analyse l'etat d'usage courant (outil, zone, sauvegarde) et propose "
			"des pistes pour debloquer ton workflow.");
		ImGui::Separator();

		// Bouton « Analyser » : (re)calcule le rapport. L'analyse N'A LIEU qu'au
		// clic (pas chaque frame) pour rester explicite et bon marché.
		if (ImGui::Button("Analyser"))
		{
			m_lastDiagnosticReport = m_diagnosticSystem.Analyze(BuildDiagnosticContext());
			m_diagnosticHasRun = true;
			LOG_INFO(Render,
				"[WorldEditorImGui] Diagnostic : {} critique(s), {} forte(s), {} astuce(s).",
				m_lastDiagnosticReport.criticalCount, m_lastDiagnosticReport.strongCount,
				m_lastDiagnosticReport.tipCount);
		}
		ImGui::SameLine();
		if (!m_diagnosticHasRun)
		{
			ImGui::TextDisabled("Aucune analyse lancee.");
			ImGui::End();
			return;
		}

		const diag::DiagnosticSystem::Report& rep = m_lastDiagnosticReport;

		// Compteurs colorés par importance (Critical rouge / Strong ambre / Tip gris).
		ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "Critiques : %u", rep.criticalCount);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "Fortes : %u", rep.strongCount);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.f), "Astuces : %u", rep.tipCount);
		ImGui::Separator();

		if (rep.IsClean())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.55f, 1.f),
				"Tu as l'air sur les bons rails ! Aucune suggestion.");
			ImGui::End();
			return;
		}

		// Liste triée par importance décroissante (garanti par DiagnosticSystem).
		// AFFICHAGE SEUL : le libellé d'action « one-click » est montré tel quel,
		// AUCUNE exécution n'est câblée ici (action réelle = passe UI séparée).
		for (size_t i = 0; i < rep.suggestions.size(); ++i)
		{
			const diag::DiagnosticSuggestion& sug = rep.suggestions[i];
			ImVec4 impColor;
			const char* impLabel;
			switch (sug.importance)
			{
			case diag::SuggestionImportance::Critical: impColor = ImVec4(1.f, 0.4f, 0.35f, 1.f);  impLabel = "[CRITIQUE]"; break;
			case diag::SuggestionImportance::Strong:   impColor = ImVec4(1.f, 0.82f, 0.35f, 1.f); impLabel = "[FORT]";     break;
			default:                                   impColor = ImVec4(0.7f, 0.7f, 0.7f, 1.f);  impLabel = "[ASTUCE]";   break;
			}
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextColored(impColor, "%s", impLabel);
			ImGui::SameLine();
			ImGui::TextWrapped("%s", sug.titleFr.c_str());
			if (!sug.explanationFr.empty())
			{
				ImGui::TextWrapped("%s", sug.explanationFr.c_str());
			}
			// Libellé d'action PROPOSÉE — affiché en italique grisé, désactivé :
			// le câblage de l'action effective + la modale de confirmation sont
			// une passe UI séparée. On ne propose donc PAS de bouton cliquable.
			if (!sug.oneClickActionLabelFr.empty())
			{
				ImGui::TextDisabled("Action proposee : %s", sug.oneClickActionLabelFr.c_str());
			}
			ImGui::PopID();
			ImGui::Separator();
		}

		ImGui::End();
	}

	namespace
	{
		/// Dessine une rangée de cartes de choix pour une étape du wizard.
		/// Chaque carte est un `Selectable` qui, au clic, applique la valeur
		/// associée via `SetChoiceForCurrentStep`. La carte correspondant à la
		/// valeur courante est mise en évidence (cadre actif ImGui).
		/// \param wizard       machine d'état du wizard (modifiée au clic).
		/// \param currentValue valeur actuellement retenue pour cette étape
		///                     (issue de `wizard.Choices()`), pour le surlignage.
		/// \param options      paires {valeur interne, libellé FR affiché}.
		/// Effet de bord : appelle `wizard.SetChoiceForCurrentStep` au clic
		/// (modifie les choix). À appeler en main thread (rendu ImGui).
		void DrawWizardChoiceCards(
			engine::editor::world::wizard::QuickStartWizard& wizard,
			const std::string& currentValue,
			const std::vector<std::pair<const char*, const char*>>& options)
		{
			for (size_t i = 0; i < options.size(); ++i)
			{
				const char* value = options[i].first;
				const char* label = options[i].second;
				const bool selected = (currentValue == value);
				ImGui::PushID(static_cast<int>(i));
				// Carte de largeur fixe, alignées horizontalement (3-4 par étape).
				if (ImGui::Selectable(label, selected, 0, ImVec2(150.0f, 60.0f)))
				{
					(void)wizard.SetChoiceForCurrentStep(value);
				}
				ImGui::PopID();
				if (i + 1 < options.size())
				{
					ImGui::SameLine();
				}
			}
		}
	} // namespace

	/// Réinitialise l'assistant à son état initial (étape Climat, choix par
	/// défaut, seed 42). Voir le `///` de déclaration pour la sémantique et le
	/// moment d'appel (transition fermé→ouvert uniquement). Effet de bord :
	/// remplace `m_wizard` par une instance neuve et remet les flags/seed UI.
	void WorldEditorImGui::ResetWizardState()
	{
		// Instance neuve : repart à WizardStep::Climate avec les WizardChoices
		// par défaut (cf. QuickStartWizard.h : pas de Reset()/Restart() dédié).
		m_wizard = engine::editor::world::wizard::QuickStartWizard{};
		// Seed UI ré-aligné sur le défaut des WizardChoices (42), puis recopié
		// dans le wizard pour que l'étape Aperçu et la génération soient cohérentes.
		m_wizardSeed = 42;
		m_wizard.SetSeed(static_cast<uint32_t>(m_wizardSeed));
		// Flags transitoires : aucune confirmation en attente, pas de bandeau de
		// résumé d'une génération précédente.
		m_wizardConfirmRequested = false;
		m_wizardHasGenerated = false;
	}

	/// Rend la fenêtre de l'assistant « Nouvelle zone » (5 étapes) + sa modale
	/// de confirmation de génération. Doit être appelée chaque frame depuis
	/// BuildUi. Effet de bord : ImGui state ; déclenche `RunWizardGeneration`
	/// à la confirmation de la modale.
	void WorldEditorImGui::RenderWizardWindow()
	{
		if (!m_showWizard)
		{
			return;
		}

		namespace wizard = engine::editor::world::wizard;

		ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Nouvelle zone (assistant)", &m_showWizard,
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}

		const wizard::WizardStep step = m_wizard.CurrentStep();
		const wizard::WizardChoices& choices = m_wizard.Choices();

		// En-tête : numéro et nom de l'étape courante (5 étapes au total).
		static const char* kStepNames[5] = {
			"1/5 - Climat", "2/5 - Relief", "3/5 - Cote", "4/5 - Points d'interet",
			"5/5 - Apercu" };
		const int stepIdx = static_cast<int>(step);
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "Etape %s",
			kStepNames[stepIdx < 0 || stepIdx > 4 ? 4 : stepIdx]);
		ImGui::Separator();

		// Corps : cartes de choix selon l'étape, ou récapitulatif à l'Aperçu.
		switch (step)
		{
		case wizard::WizardStep::Climate:
			ImGui::TextWrapped("Choisis le climat dominant de la zone.");
			ImGui::Spacing();
			DrawWizardChoiceCards(m_wizard, choices.climate, {
				{ "temperate", "Tempere" }, { "arid", "Aride" },
				{ "polar", "Polaire" }, { "tropical", "Tropical" } });
			break;
		case wizard::WizardStep::Relief:
			ImGui::TextWrapped("Choisis le relief general du terrain.");
			ImGui::Spacing();
			DrawWizardChoiceCards(m_wizard, choices.relief, {
				{ "plains", "Plaines" }, { "hills", "Collines" },
				{ "mountains", "Montagnes" }, { "escarped", "Escarpe" } });
			break;
		case wizard::WizardStep::Coast:
			ImGui::TextWrapped("Quel rapport la zone a-t-elle a la cote ?");
			ImGui::Spacing();
			DrawWizardChoiceCards(m_wizard, choices.coast, {
				{ "interior", "Interieur" }, { "moderate", "Cote moderee" },
				{ "dramatic", "Cote spectaculaire" } });
			break;
		case wizard::WizardStep::Poi:
			ImGui::TextWrapped("Ajoute un point d'interet principal (optionnel).");
			ImGui::Spacing();
			DrawWizardChoiceCards(m_wizard, choices.poi, {
				{ "none", "Aucun" }, { "cave", "Grotte" },
				{ "ruin", "Ruine" }, { "dungeon", "Donjon" } });
			break;
		case wizard::WizardStep::Preview:
		default:
			ImGui::TextWrapped("Recapitulatif des choix. Ajuste le seed puis genere "
				"la zone.");
			ImGui::Spacing();
			ImGui::BulletText("Climat : %s", choices.climate.c_str());
			ImGui::BulletText("Relief : %s", choices.relief.c_str());
			ImGui::BulletText("Cote   : %s", choices.coast.c_str());
			ImGui::BulletText("POI    : %s", choices.poi.c_str());
			ImGui::Spacing();
			// Champ seed → SetSeed (le wizard ne valide pas le seed).
			if (ImGui::InputInt("Seed RNG", &m_wizardSeed, 1, 100))
			{
				if (m_wizardSeed < 0) m_wizardSeed = 0;
				m_wizard.SetSeed(static_cast<uint32_t>(m_wizardSeed));
			}
			break;
		}

		// Pied de page : navigation + (à l'Aperçu) bouton Générer.
		ImGui::Separator();

		// Précédent : actif sauf à la 1re étape.
		const bool canPrev = (step != wizard::WizardStep::Climate);
		if (!canPrev) ImGui::BeginDisabled();
		if (ImGui::Button("Precedent", ImVec2(120.0f, 0.0f)) && canPrev)
		{
			(void)m_wizard.Prev();
		}
		if (!canPrev) ImGui::EndDisabled();

		ImGui::SameLine();

		if (step != wizard::WizardStep::Preview)
		{
			// Suivant : gardé par CanProceed (choix valide pour l'étape courante).
			const bool canNext = m_wizard.CanProceed();
			if (!canNext) ImGui::BeginDisabled();
			if (ImGui::Button("Suivant", ImVec2(120.0f, 0.0f)) && canNext)
			{
				(void)m_wizard.Next();
			}
			if (!canNext) ImGui::EndDisabled();
			if (!canNext && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Selectionne un choix pour continuer.");
			}
		}
		else
		{
			// Étape Aperçu : bouton Générer actif seulement si prêt.
			const bool canGenerate = m_wizard.IsReadyToGenerate();
			if (!canGenerate) ImGui::BeginDisabled();
			if (ImGui::Button("Generer", ImVec2(120.0f, 0.0f)) && canGenerate)
			{
				// Demande la modale de confirmation (poussée plus bas, hors de la
				// pile de widgets disabled).
				m_wizardConfirmRequested = true;
			}
			if (!canGenerate) ImGui::EndDisabled();
		}

		ImGui::SameLine();
		if (ImGui::Button("Fermer", ImVec2(120.0f, 0.0f)))
		{
			m_showWizard = false;
		}

		// Bandeau de résumé de la dernière génération (sous le pied de page).
		if (m_wizardHasGenerated)
		{
			ImGui::Separator();
			ImGui::Text("Derniere generation : '%s'", m_wizardLastPresetId.c_str());
			ImGui::Text("Commandes poussees : %u  -  Ignorees : %u  -  Echecs : %u",
				m_wizardLastSummary.commandsPushed,
				m_wizardLastSummary.unsupportedSkipped,
				m_wizardLastSummary.failed);
			if (m_wizardLastSummary.unsupportedSkipped > 0u)
			{
				ImGui::TextWrapped("Note : %u operation(s) non supportee(s) ont ete "
					"ignorees (coastline / river_network / hydraulic_erosion / "
					"thermal_wind_erosion sans chemin d'execution complet ici).",
					m_wizardLastSummary.unsupportedSkipped);
			}
		}

		// Ouverture de la modale de confirmation (au prochain frame après le clic
		// « Generer »). On la pousse hors de tout bloc disabled.
		if (m_wizardConfirmRequested)
		{
			ImGui::OpenPopup("Confirmer la generation##wizard_confirm");
			m_wizardConfirmRequested = false;
		}

		// Modale de confirmation : la génération RÉINITIALISE la zone courante
		// (reset destructif de l'executor, non annulable — P0 connu partagé avec
		// le ZonePresetDialog). On recommande de sauvegarder d'abord.
		if (ImGui::BeginPopupModal("Confirmer la generation##wizard_confirm", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f),
				"ATTENTION : action destructive et NON annulable.");
			ImGui::Spacing();
			ImGui::TextWrapped("La generation VIDE la zone courante (terrain, eau, "
				"mesh inserts, portails de donjon) avant de la reconstruire. Cette "
				"reinitialisation n'est pas annulable (Ctrl+Z ne la restaurera pas). "
				"Sauvegarde la zone avant de continuer si tu veux la conserver.");
			ImGui::Separator();
			if (ImGui::Button("Generer (vider et reconstruire)", ImVec2(260.0f, 0.0f)))
			{
				RunWizardGeneration();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Annuler", ImVec2(120.0f, 0.0f)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::End();
	}
#else
	void WorldEditorImGui::RenderValidationPanel() {}
	void WorldEditorImGui::RenderGuidanceOverlay() {}
	void WorldEditorImGui::RenderDiagnosticPanel() {}
	void WorldEditorImGui::RenderWizardWindow() {}
#endif

	bool WorldEditorImGui::WantsCaptureMouse() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#else
		return false;
#endif
	}

	bool WorldEditorImGui::WantsCaptureKeyboard() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
#else
		return false;
#endif
	}

	bool WorldEditorImGui::RecordToBackbuffer(VkCommandBuffer cmd,
		VkImage backbufferImage,
		VkImageView backbufferView,
		VkExtent2D extent,
		const engine::render::VkDeviceContext& deviceContext)
	{
#if !defined(_WIN32)
		(void)cmd;
		(void)backbufferImage;
		(void)backbufferView;
		(void)extent;
		(void)deviceContext;
		return false;
#else
		if (!m_ready || cmd == VK_NULL_HANDLE || backbufferImage == VK_NULL_HANDLE || backbufferView == VK_NULL_HANDLE
			|| extent.width == 0 || extent.height == 0)
		{
			return false;
		}
		ImDrawData* dd = ImGui::GetDrawData();
		if (!dd || dd->DisplaySize.x <= 0.f || dd->DisplaySize.y <= 0.f || dd->CmdListsCount == 0)
		{
			return false;
		}

		VkImageMemoryBarrier toColor{};
		toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toColor.image = backbufferImage;
		toColor.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toColor);

		bool usedKhr = false;
		if (!BeginDynamicRenderingUi(cmd, backbufferView, extent, deviceContext, usedKhr))
		{
			LOG_ERROR(Render, "[WorldEditorImGui] dynamic rendering indisponible pour ImGui");
			return false;
		}

		ImGui_ImplVulkan_RenderDrawData(dd, cmd, VK_NULL_HANDLE);

		EndDynamicRenderingUi(cmd, deviceContext, usedKhr);

		VkImageMemoryBarrier toPresent{};
		toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toPresent.dstAccessMask = 0;
		toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toPresent.image = backbufferImage;
		toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toPresent);

		return true;
#endif
	}

	void WorldEditorImGui::AttachPlatformWindow(void* hwndNative, engine::platform::Window& window)
	{
#if defined(_WIN32)
		m_hwnd = hwndNative;
		window.SetPreMessageInterceptor([this](uint32_t msg, uint64_t wp, int64_t lp) -> intptr_t {
			if (!m_ready || !m_hwnd)
			{
				return 0;
			}
			return static_cast<intptr_t>(ImGui_ImplWin32_WndProcHandler(
				static_cast<HWND>(m_hwnd), msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp)));
		});
#else
		(void)hwndNative;
		(void)window;
#endif
	}

#if defined(_WIN32)
	// ── Réorganisation UI 2026-07-17 : registre d'actions + menus français ──
	// (spec docs/superpowers/specs/2026-07-17-editor-menus-toolbar-reorg-design.md)

	void WorldEditorImGui::RegisterEditorActions()
	{
		if (m_actionsRegistered || m_shell == nullptr) return;
		m_actionsRegistered = true;

		namespace weact = engine::editor::world::actions;
		using engine::editor::world::ActiveTool;
		weact::EditorActionRegistry& reg = m_shell->MutableActionRegistry();

		// Helper local : construit + enregistre une action en une expression.
		auto add = [&reg](const char* id, const char* label,
			weact::ActionCategory cat, const char* section, const char* shortcut,
			std::function<bool()> enabled, std::function<bool()> checked,
			std::function<void()> execute)
		{
			weact::EditorAction a;
			a.id = id;
			a.label = label;
			a.category = cat;
			a.section = (section != nullptr) ? section : "";
			a.shortcutText = (shortcut != nullptr) ? shortcut : "";
			a.enabled = std::move(enabled);
			a.checked = std::move(checked);
			a.execute = std::move(execute);
			(void)reg.Register(std::move(a));
		};

		// ---- Fichier --------------------------------------------------------
		add("file.new-zone-wizard", "Nouvelle zone (assistant)...",
			weact::ActionCategory::Fichier, "Nouveau", nullptr,
			[this] { return m_shell != nullptr; }, nullptr,
			[this]
			{
				// Transition fermé→ouvert : repartir de l'étape 1 sans état
				// résiduel d'une session précédente (choix, seed, résumé).
				if (!m_showWizard) { ResetWizardState(); }
				m_showWizard = true;
			});
		add("file.zone-preset", "Appliquer un preset de zone...",
			weact::ActionCategory::Fichier, "Nouveau", nullptr,
			[this] { return m_shell != nullptr && m_zonePresetDialog != nullptr; }, nullptr,
			[this] { if (m_zonePresetDialog) { m_zonePresetDialog->Open(); } });
		add("file.save", "Sauvegarder la carte courante",
			weact::ActionCategory::Fichier, "Enregistrer", "Ctrl+S",
			[this] { return m_session != nullptr && m_cfg != nullptr; }, nullptr,
			[this] { (void)SaveCurrentMapAndNote(); });
		add("file.import.texture", "Importer une texture (PNG/JPG/TGA/BMP)...",
			weact::ActionCategory::Fichier, "Import", nullptr,
			[this] { return m_session != nullptr && m_cfg != nullptr; }, nullptr,
			[this] { if (m_session && m_cfg) { (void)m_session->ActionImportTexture(*m_cfg); } });
		add("file.import.audio", "Importer un son (WAV/OGG)...",
			weact::ActionCategory::Fichier, "Import", nullptr,
			[this] { return m_session != nullptr && m_cfg != nullptr; }, nullptr,
			[this] { if (m_session && m_cfg) { (void)m_session->ActionImportAudio(*m_cfg); } });
		add("zone.validate", "Valider la zone",
			weact::ActionCategory::Fichier, "Export", nullptr,
			[this] { return m_shell != nullptr; }, nullptr,
			[this] { RunZoneValidation(); });
		// Export runtime BLOQUÉ si la dernière validation a remonté des
		// erreurs (comportement Lot C vague 4 conservé). Reste actif si
		// aucune validation n'a encore tourné.
		add("zone.export", "Exporter en runtime",
			weact::ActionCategory::Fichier, "Export", nullptr,
			[this]
			{
				const bool blocked = m_validationHasRun
					&& m_lastValidationReport.HasBlockingErrors();
				return m_session != nullptr && m_cfg != nullptr && !blocked;
			},
			nullptr,
			[this] { if (m_session && m_cfg) { (void)m_session->ActionExportRuntime(*m_cfg); } });
		add("file.quit", "Quitter",
			weact::ActionCategory::Fichier, nullptr, nullptr,
			[this] { return static_cast<bool>(m_onQuitRequested); }, nullptr,
			[this]
			{
				// Modifications non sauvegardées → confirmation ; sinon
				// fermeture directe via le callback Engine::OnQuit.
				if (m_shell != nullptr && m_shell->IsDirtySinceSave())
				{
					m_quitConfirmRequested = true;
				}
				else if (m_onQuitRequested)
				{
					m_onQuitRequested();
				}
			});

		// ---- Édition (les actions undo/redo/historique sont enregistrées
		// par le shell lui-même, cf. WorldEditorShell::RegisterShellActions) --
		add("edit.preferences", "Préférences...",
			weact::ActionCategory::Edition, nullptr, nullptr,
			nullptr, nullptr,
			[this] { m_showPreferencesWindow = true; });
		// PR 3 — palette de commandes + fenêtre récapitulative des raccourcis.
		add("app.command-palette", "Palette de commandes...",
			weact::ActionCategory::Edition, nullptr, "Ctrl+P",
			nullptr, nullptr,
			[this] { OpenCommandPalette(); });
		add("help.shortcuts", "Raccourcis clavier...",
			weact::ActionCategory::Aide, nullptr, nullptr,
			nullptr,
			[this] { return m_showShortcutsWindow; },
			[this] { m_showShortcutsWindow = !m_showShortcutsWindow; });

		// ---- Vue (options du viewport uniquement) ---------------------------
		add("view.grid", "Grille (afficher/masquer)",
			weact::ActionCategory::Vue, nullptr, nullptr,
			[this] { return m_session != nullptr; },
			[this] { return m_session != nullptr && m_session->ShowGrid(); },
			[this] { if (m_session) { m_session->ShowGrid() = !m_session->ShowGrid(); } });
		add("view.camera-help", "Aide caméra (instructions WASD)",
			weact::ActionCategory::Vue, nullptr, nullptr,
			nullptr,
			[this] { return m_showCameraHelp; },
			[this] { m_showCameraHelp = !m_showCameraHelp; });
		add("view.atmosphere", "Atmosphère (jour/nuit)",
			weact::ActionCategory::Vue, nullptr, nullptr,
			nullptr,
			[this] { return m_showAtmospherePanel; },
			[this] { m_showAtmospherePanel = !m_showAtmospherePanel; });

		// ---- Fenêtre (toggles hors panneaux du shell) -----------------------
		add("window.texture-library", "Bibliothèque de textures",
			weact::ActionCategory::Fenetre, nullptr, nullptr,
			nullptr,
			[this] { return m_showTextureLibrary; },
			[this] { m_showTextureLibrary = !m_showTextureLibrary; });
		add("window.validation-panel", "Validation de zone",
			weact::ActionCategory::Fenetre, nullptr, nullptr,
			nullptr,
			[this] { return m_showValidationPanel; },
			[this] { m_showValidationPanel = !m_showValidationPanel; });
		add("window.layout.reset", "Réinitialiser la disposition des fenêtres",
			weact::ActionCategory::Fenetre, "Disposition", nullptr,
			nullptr, nullptr,
			[this] { ResetDockLayout(); });

		// ---- Outils (les 15 outils du shell, groupés par famille) ----------
		// Un clic sur l'outil déjà actif le désélectionne (retour à None) —
		// même convention que le bouton X de la barre d'icônes.
		struct ToolActionSpec
		{
			const char* id;
			const char* label;
			const char* section;
			const char* shortcut;
			ActiveTool  tool;
		};
		static constexpr ToolActionSpec kToolActions[] = {
			{ "tool.terrain-sculpt", "Sculpture du terrain", "Terrain", "B", ActiveTool::TerrainSculpt },
			{ "tool.terrain-stamp", "Tampon de terrain", "Terrain", "N", ActiveTool::TerrainStamp },
			{ "tool.splat-paint", "Peinture de texture (sol)", "Terrain", "P", ActiveTool::SplatPaint },
			{ "tool.lake", "Lac", "Eau", "L", ActiveTool::Lake },
			{ "tool.river", "Rivière", "Eau", "R", ActiveTool::River },
			{ "tool.river-network", "Réseau fluvial", "Eau", "Ctrl+Shift+N", ActiveTool::RiverNetwork },
			{ "tool.coastline", "Littoral", "Eau", "Ctrl+Shift+C", ActiveTool::Coastline },
			{ "tool.mountain-range", "Chaîne de montagnes", "Macro", "Ctrl+Shift+M", ActiveTool::MountainRange },
			{ "tool.valley-chain", "Chaîne de vallées", "Macro", "Ctrl+Shift+V", ActiveTool::ValleyChain },
			{ "tool.hydraulic-erosion", "Érosion hydraulique", "Macro", "Ctrl+Shift+H", ActiveTool::HydraulicErosion },
			{ "tool.thermal-wind-erosion", "Érosion thermique/vent", "Macro", "Ctrl+Shift+T", ActiveTool::ThermalWindErosion },
			{ "tool.cave", "Grotte", "Structures", "Ctrl+Shift+G", ActiveTool::Cave },
			{ "tool.overhang", "Surplomb", "Structures", "Ctrl+Shift+O", ActiveTool::Overhang },
			{ "tool.arch", "Arche", "Structures", "Ctrl+Shift+A", ActiveTool::Arch },
			{ "tool.dungeon-portal", "Portail de donjon", "Structures", "Ctrl+Shift+D", ActiveTool::DungeonPortal },
		};
		for (const ToolActionSpec& spec : kToolActions)
		{
			const ActiveTool tool = spec.tool;
			add(spec.id, spec.label, weact::ActionCategory::Outils,
				spec.section, spec.shortcut,
				[this] { return m_shell != nullptr; },
				[this, tool] { return m_shell != nullptr && m_shell->GetActiveTool() == tool; },
				[this, tool]
				{
					if (m_shell == nullptr) return;
					m_shell->SetActiveTool(
						m_shell->GetActiveTool() == tool ? ActiveTool::None : tool);
				});
		}

		// ---- Aide -----------------------------------------------------------
		add("help.diagnostic", "Diagnostic (pourquoi ça ne marche pas ?)",
			weact::ActionCategory::Aide, nullptr, nullptr,
			nullptr,
			[this] { return m_showDiagnosticPanel; },
			[this] { m_showDiagnosticPanel = !m_showDiagnosticPanel; });
		add("help.about", "À propos...",
			weact::ActionCategory::Aide, nullptr, nullptr,
			nullptr, nullptr,
			[this] { m_showAboutWindow = true; });
	}

	bool WorldEditorImGui::MenuItemForAction(const char* id)
	{
		namespace weact = engine::editor::world::actions;
		const weact::EditorAction* a =
			(m_shell != nullptr) ? m_shell->GetActionRegistry().Find(id) : nullptr;
		if (a == nullptr) return false;
		const bool enabled  = weact::EditorActionRegistry::IsEnabled(*a);
		const bool checked  = a->checked ? a->checked() : false;
		const char* shortcut = a->shortcutText.empty() ? nullptr : a->shortcutText.c_str();
		if (ImGui::MenuItem(a->label.c_str(), shortcut, checked, enabled))
		{
			if (a->execute) { a->execute(); }
			return true;
		}
		return false;
	}

	void WorldEditorImGui::RenderMenuBarFr()
	{
		namespace weact = engine::editor::world::actions;
		namespace prefs = engine::editor::world::prefs;
		if (!ImGui::BeginMainMenuBar()) return;

		const bool sessionReady = (m_session != nullptr && m_cfg != nullptr);

		if (ImGui::BeginMenu("Fichier"))
		{
			ImGui::SeparatorText("Nouveau");
			MenuItemForAction("file.new-zone-wizard");
			// Rectangle-cible du guidance overlay (id stable, convention
			// <panel>.<sous>.<id> — Lot C vague 4).
			m_widgetTargets.Register("menubar.file.new_zone_wizard",
				{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
				  ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y });
			MenuItemForAction("file.zone-preset");

			ImGui::SeparatorText("Ouvrir");
			if (sessionReady && !m_session->AvailableMapsScanned())
			{
				m_session->RefreshAvailableMaps(*m_cfg);
			}
			ImGui::BeginDisabled(!sessionReady);
			if (ImGui::BeginMenu("Charger une carte"))
			{
				const std::vector<std::string>& mapIds = m_session->AvailableMapIds();
				if (mapIds.empty())
				{
					ImGui::TextDisabled("Aucune carte. Créez-en une via le panneau 'Carte'.");
				}
				else
				{
					for (size_t i = 0; i < mapIds.size(); ++i)
					{
						if (ImGui::MenuItem(mapIds[i].c_str())
							&& m_session->ActionLoadMapByZoneId(*m_cfg, mapIds[i]))
						{
							m_session->SelectedAvailableMapIndex() = static_cast<int>(i);
							prefs::UserPrefsStore::Instance().PushRecentMap(mapIds[i]);
						}
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Rafraîchir la liste"))
				{
					m_session->RefreshAvailableMaps(*m_cfg);
				}
				ImGui::EndMenu();
			}
			// Cartes récentes (user_prefs.json) : entrée grisée si la carte
			// n'existe plus dans world_editor/maps/ (supprimée hors éditeur).
			if (ImGui::BeginMenu("Cartes récentes"))
			{
				const std::vector<std::string>& recents =
					prefs::UserPrefsStore::Instance().GetRecentMaps();
				if (recents.empty())
				{
					ImGui::TextDisabled("Aucune carte récente.");
				}
				else
				{
					const std::vector<std::string>& mapIds = m_session->AvailableMapIds();
					for (const std::string& zoneId : recents)
					{
						const bool available =
							std::find(mapIds.begin(), mapIds.end(), zoneId) != mapIds.end();
						if (ImGui::MenuItem(zoneId.c_str(), nullptr, false, available)
							&& m_session->ActionLoadMapByZoneId(*m_cfg, zoneId))
						{
							prefs::UserPrefsStore::Instance().PushRecentMap(zoneId);
						}
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndDisabled();

			ImGui::SeparatorText("Enregistrer");
			MenuItemForAction("file.save");

			ImGui::SeparatorText("Import");
			MenuItemForAction("file.import.texture");
			MenuItemForAction("file.import.audio");

			ImGui::SeparatorText("Export");
			MenuItemForAction("zone.validate");
			m_widgetTargets.Register("toolbar.button.validate",
				{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
				  ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y });
			MenuItemForAction("zone.export");
			m_widgetTargets.Register("menubar.file.export_runtime",
				{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
				  ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y });
			{
				const bool exportBlocked = m_validationHasRun
					&& m_lastValidationReport.HasBlockingErrors();
				if (exportBlocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Export bloqué : corrigez les erreurs de validation (panneau Validation).");
				}
			}

			ImGui::Separator();
			MenuItemForAction("file.quit");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Édition"))
		{
			MenuItemForAction("edit.undo");
			MenuItemForAction("edit.redo");
			MenuItemForAction("edit.history");
			ImGui::Separator();
			MenuItemForAction("app.command-palette");
			MenuItemForAction("edit.preferences");
			MenuItemForAction("help.shortcuts");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Vue"))
		{
			MenuItemForAction("view.grid");
			MenuItemForAction("view.camera-help");
			MenuItemForAction("view.atmosphere");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Fenêtre"))
		{
			ImGui::SeparatorText("Panneaux");
			if (m_shell != nullptr)
			{
				// Toggles de panneaux enregistrés par le shell (ordre stable).
				for (const weact::EditorAction& a : m_shell->GetActionRegistry().Actions())
				{
					if (a.id.rfind("window.panel.", 0) == 0)
					{
						MenuItemForAction(a.id.c_str());
					}
				}
			}
			MenuItemForAction("window.texture-library");
			MenuItemForAction("window.validation-panel");
			ImGui::SeparatorText("Disposition");
			MenuItemForAction("window.layout.reset");
			ImGui::TextDisabled("Astuce : faites glisser une fenêtre par sa barre de titre");
			ImGui::TextDisabled("pour la docker à gauche, à droite ou en bas.");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Outils"))
		{
			if (m_shell == nullptr)
			{
				ImGui::TextDisabled("Shell éditeur non branché.");
			}
			else
			{
				// Sous-menus par famille — même source (registre, section)
				// que la future palette d'outils et la palette Ctrl+P.
				static constexpr const char* kFamilies[] =
					{ "Terrain", "Eau", "Macro", "Structures" };
				for (const char* family : kFamilies)
				{
					if (!ImGui::BeginMenu(family)) continue;
					for (const weact::EditorAction& a : m_shell->GetActionRegistry().Actions())
					{
						if (a.category == weact::ActionCategory::Outils
							&& a.section == family)
						{
							MenuItemForAction(a.id.c_str());
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Aide"))
		{
			MenuItemForAction("help.diagnostic");
			MenuItemForAction("help.shortcuts");
			ImGui::Separator();
			MenuItemForAction("help.about");
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	bool WorldEditorImGui::SaveCurrentMapAndNote()
	{
		if (m_session == nullptr || m_cfg == nullptr) return false;
		if (!m_session->ActionSaveCurrentMap(*m_cfg)) return false;
		if (m_shell != nullptr)
		{
			m_shell->NoteSaved();
		}
		engine::editor::world::prefs::UserPrefsStore::Instance()
			.PushRecentMap(m_session->Doc().zoneId);
		return true;
	}

	void WorldEditorImGui::ResetDockLayout()
	{
		// Réinitialisation in-process : on retire le node DockBuilder courant
		// et on repasse m_defaultLayoutAttempted à false. La frame suivante
		// reconstruit la disposition par défaut via le bloc DockBuilder en
		// haut de BuildUi().
		const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceV2");
		ImGui::DockBuilderRemoveNode(dockId);
		m_defaultLayoutAttempted = false;
		// Supprime aussi le fichier .ini pour que la réinitialisation persiste
		// après le redémarrage (sinon ImGui rechargerait l'ancienne disposition
		// au prochain run).
		std::error_code ec;
		std::filesystem::remove("world_editor_imgui.ini", ec);
		// Polish UI 2026-07-17 — la disposition est désormais UNIFIÉE : les
		// panneaux du shell sont dockés par la même disposition par défaut.
		// L'ancien layout du shell (`editor_world_layout.ini`) ne doit donc
		// ni être rechargé (lecture différée annulée) ni survivre sur disque
		// (il réappliquerait des positions périmées par-dessus).
		if (m_shell != nullptr)
		{
			m_shell->DiscardPendingLayoutLoad();
			if (!m_shell->LayoutPath().empty())
			{
				std::filesystem::remove(m_shell->LayoutPath(), ec);
			}
		}
		if (m_session)
		{
			m_session->SetStatus("Disposition réinitialisée.");
		}
	}

	void WorldEditorImGui::RenderQuitConfirmModal()
	{
		if (m_quitConfirmRequested)
		{
			ImGui::OpenPopup("Quitter l'éditeur ?");
			m_quitConfirmRequested = false;
		}
		// Centre la modale sur le viewport (confort ; pas d'état persisté).
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(
			ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
			       vp->WorkPos.y + vp->WorkSize.y * 0.5f),
			ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal("Quitter l'éditeur ?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Des modifications non sauvegardées existent.");
			ImGui::TextDisabled("Carte : %s",
				(m_session != nullptr) ? m_session->Doc().zoneId.c_str() : "(inconnue)");
			ImGui::Separator();
			if (ImGui::Button("Sauvegarder et quitter"))
			{
				if (SaveCurrentMapAndNote() && m_onQuitRequested)
				{
					m_onQuitRequested();
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Quitter sans sauvegarder"))
			{
				if (m_onQuitRequested) { m_onQuitRequested(); }
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Annuler"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void WorldEditorImGui::RenderPreferencesWindow()
	{
		if (!m_showPreferencesWindow) return;
		ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Préférences", &m_showPreferencesWindow,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			// -- Clavier (migré de l'ancien menu Options) ---------------------
			ImGui::SeparatorText("Clavier");
			if (m_cfg != nullptr)
			{
				const std::string cur = m_cfg->GetString("controls.movement_layout", "wasd");
				const bool zqsdActive = (cur == "zqsd");
				if (ImGui::RadioButton("QWERTY (WASD)", !zqsdActive) && zqsdActive)
				{
					m_cfg->SetValue("controls.movement_layout", std::string("wasd"));
					TryPersistMovementLayoutToUserSettings("wasd");
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("AZERTY (ZQSD)", zqsdActive) && !zqsdActive)
				{
					m_cfg->SetValue("controls.movement_layout", std::string("zqsd"));
					TryPersistMovementLayoutToUserSettings("zqsd");
				}
			}

			// -- Caméra (migré de l'ancien menu Vue) --------------------------
			ImGui::SeparatorText("Caméra");
			if (m_cfg != nullptr)
			{
				float mult = static_cast<float>(
					m_cfg->GetDouble("controls.editor_camera_speed_multiplier", 1.0));
				ImGui::TextDisabled("Vitesse de déplacement (Shift = course) :");
				if (ImGui::SliderFloat("Vitesse caméra (x)", &mult, 0.25f, 5.0f, "%.2f"))
				{
					mult = std::clamp(mult, 0.25f, 5.0f);
					m_cfg->SetValue("controls.editor_camera_speed_multiplier",
						static_cast<double>(mult));
				}
				ImGui::TextDisabled("Astuce : montez ce curseur pour traverser plus vite les");
				ImGui::TextDisabled("grandes cartes pendant la création.");
			}

			// -- Mode éditeur (migré de l'ancien menu Options, M100.45) -------
			ImGui::SeparatorText("Mode éditeur");
			{
				namespace modes = engine::editor::world::modes;
				auto& reg = modes::EditorModeRegistry::Instance();
				const modes::EditorMode current = reg.GetCurrentMode();
				if (ImGui::RadioButton("Simple (recommandé pour démarrer)",
					current == modes::EditorMode::Simple))
				{
					reg.SetCurrentMode(modes::EditorMode::Simple);
				}
				if (ImGui::RadioButton("Avancé (accès complet aux paramètres)",
					current == modes::EditorMode::Advanced))
				{
					reg.SetCurrentMode(modes::EditorMode::Advanced);
				}
			}
		}
		ImGui::End();
	}

	void WorldEditorImGui::RenderAboutWindow()
	{
		if (!m_showAboutWindow) return;
		if (ImGui::Begin("À propos", &m_showAboutWindow,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("LCDLLN World Editor");
			ImGui::TextDisabled("Éditeur de cartes interne — binaire lcdlln_world_editor.exe");
			ImGui::Separator();
			ImGui::TextDisabled("Spec UI : docs/superpowers/specs/");
			ImGui::TextDisabled("2026-07-17-editor-menus-toolbar-reorg-design.md");
		}
		ImGui::End();
	}
	void WorldEditorImGui::RenderCommandPalette()
	{
		if (!m_showCommandPalette) return;
		namespace weact = engine::editor::world::actions;
		namespace wpal  = engine::editor::world::palette;

		// Fenêtre centrée horizontalement, ancrée vers le haut (style UE /
		// VS Code). Taille figée : la liste défile dans un child.
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(
			ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 72.0f),
			ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_Always);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoSavedSettings;
		if (!ImGui::Begin("Palette de commandes", &m_showCommandPalette, flags))
		{
			ImGui::End();
			return;
		}

		// Champ de recherche avec focus automatique à l'ouverture.
		if (m_paletteFocusQuery)
		{
			ImGui::SetKeyboardFocusHere();
			m_paletteFocusQuery = false;
		}
		ImGui::SetNextItemWidth(-1.0f);
		const bool enterPressed = ImGui::InputTextWithHint("##palette_query",
			"Commencez à taper pour rechercher une action...",
			m_paletteQuery, sizeof(m_paletteQuery),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// Snapshot des actions du registre → entrées texte de la palette.
		std::vector<wpal::PaletteEntry> entries;
		if (m_shell != nullptr)
		{
			static constexpr const char* kCategoryFr[] =
				{ "Fichier", "Édition", "Vue", "Fenêtre", "Outils", "Aide" };
			const auto& actionsVec = m_shell->GetActionRegistry().Actions();
			entries.reserve(actionsVec.size());
			for (const weact::EditorAction& a : actionsVec)
			{
				wpal::PaletteEntry e;
				e.id = a.id;
				e.label = a.label;
				const size_t catIdx = static_cast<size_t>(a.category);
				e.categoryFr = (catIdx < 6u) ? kCategoryFr[catIdx] : "";
				e.shortcutText = a.shortcutText;
				e.enabled = weact::EditorActionRegistry::IsEnabled(a);
				entries.push_back(std::move(e));
			}
		}
		const std::vector<size_t> order =
			wpal::FilterPaletteEntries(m_paletteQuery, entries);

		// Navigation clavier ↑/↓ dans la liste filtrée.
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { ++m_paletteSelected; }
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   { --m_paletteSelected; }
		if (order.empty())
		{
			m_paletteSelected = 0;
		}
		else
		{
			m_paletteSelected = std::clamp(m_paletteSelected, 0,
				static_cast<int>(order.size()) - 1);
		}

		// Liste filtrée : libellé à gauche, catégorie + raccourci grisés à
		// droite. Les actions désactivées sont affichées grisées et ne sont
		// pas exécutables.
		const weact::EditorAction* toExecute = nullptr;
		ImGui::BeginChild("##palette_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
		for (size_t rank = 0; rank < order.size(); ++rank)
		{
			const wpal::PaletteEntry& e = entries[order[rank]];
			const bool selected = (static_cast<int>(rank) == m_paletteSelected);

			ImGui::PushID(static_cast<int>(rank));
			if (!e.enabled) { ImGui::BeginDisabled(); }
			if (ImGui::Selectable(e.label.c_str(), selected))
			{
				m_paletteSelected = static_cast<int>(rank);
				if (e.enabled && m_shell != nullptr)
				{
					toExecute = m_shell->GetActionRegistry().Find(e.id);
				}
			}
			if (!e.enabled) { ImGui::EndDisabled(); }

			// Colonne droite : catégorie + raccourci.
			std::string right = e.categoryFr;
			if (!e.shortcutText.empty())
			{
				right += "  ";
				right += e.shortcutText;
			}
			if (!right.empty())
			{
				ImGui::SameLine();
				const float w = ImGui::CalcTextSize(right.c_str()).x;
				const float avail = ImGui::GetContentRegionAvail().x;
				if (avail > w + 8.0f)
				{
					ImGui::SameLine(0.0f, avail - w - 4.0f);
				}
				ImGui::TextDisabled("%s", right.c_str());
			}
			if (selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow)
				|| ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
			{
				ImGui::SetScrollHereY();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();

		// Entrée = exécute la sélection courante de la liste filtrée.
		if (enterPressed && !order.empty() && m_shell != nullptr)
		{
			const wpal::PaletteEntry& e =
				entries[order[static_cast<size_t>(m_paletteSelected)]];
			if (e.enabled)
			{
				toExecute = m_shell->GetActionRegistry().Find(e.id);
			}
		}
		if (toExecute != nullptr)
		{
			m_showCommandPalette = false;
			if (toExecute->execute) { toExecute->execute(); }
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_showCommandPalette = false;
		}
		ImGui::End();
	}

	void WorldEditorImGui::RenderShortcutsWindow()
	{
		if (!m_showShortcutsWindow) return;
		namespace weact = engine::editor::world::actions;

		ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Raccourcis clavier", &m_showShortcutsWindow))
		{
			ImGui::End();
			return;
		}

		// Section 1 — raccourcis d'actions, générés depuis le registre :
		// ajouter une action avec `shortcutText` la fait apparaître ici sans
		// maintenance manuelle.
		if (m_shell != nullptr)
		{
			static constexpr const char* kCategoryFr[] =
				{ "Fichier", "Édition", "Vue", "Fenêtre", "Outils", "Aide" };
			for (size_t cat = 0; cat < 6u; ++cat)
			{
				bool headerShown = false;
				for (const weact::EditorAction& a : m_shell->GetActionRegistry().Actions())
				{
					if (static_cast<size_t>(a.category) != cat) continue;
					if (a.shortcutText.empty()) continue;
					if (!headerShown)
					{
						ImGui::SeparatorText(kCategoryFr[cat]);
						headerShown = true;
					}
					ImGui::TextUnformatted(a.label.c_str());
					ImGui::SameLine(320.0f);
					ImGui::TextDisabled("%s", a.shortcutText.c_str());
				}
			}
		}

		// Section 2 — raccourcis hors registre (dispatchés par Engine /
		// WorldEditorShell::HandleShortcut) : documentés statiquement ici.
		struct StaticShortcut { const char* label; const char* keys; };
		static constexpr StaticShortcut kStaticShortcuts[] = {
			{ "Déplacement caméra",              "WASD / ZQSD (cf. Préférences)" },
			{ "Course (caméra plus rapide)",     "Shift" },
			{ "Mode caméra FPS / Orbital / Top", "Pavé num. 1 / 3 / 7" },
			{ "Panneaux du shell",               "F1..F12" },
			{ "Annuler / Rétablir",              "Ctrl+Z / Ctrl+Y" },
			{ "Palette de commandes",            "Ctrl+P" },
		};
		ImGui::SeparatorText("Caméra & navigation");
		for (const StaticShortcut& s : kStaticShortcuts)
		{
			ImGui::TextUnformatted(s.label);
			ImGui::SameLine(320.0f);
			ImGui::TextDisabled("%s", s.keys);
		}

		ImGui::End();
	}
#endif // _WIN32

	void WorldEditorImGui::DetachPlatformWindow(engine::platform::Window& window)
	{
#if defined(_WIN32)
		window.SetPreMessageInterceptor({});
#endif
		m_hwnd = nullptr;
		(void)window;
	}

} // namespace engine::editor
