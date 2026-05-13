#include "src/world_editor/core/WorldEditorShell.h"

#include "src/world_editor/camera/EditorCameraController.h"
#include "src/world_editor/panels/ScenePanel.h"
#include "src/world_editor/panels/InspectorPanel.h"
#include "src/world_editor/panels/AssetBrowserPanel.h"
#include "src/world_editor/panels/OutlinerPanel.h"
#include "src/world_editor/panels/ConsolePanel.h"
#include "src/world_editor/panels/ToolPropertiesPanel.h"
#include "src/world_editor/panels/HistoryPanel.h"
#include "src/world_editor/panels/SurfaceTablePanel.h"
#include "src/world_editor/panels/CollisionEditorPanel.h"
#include "src/world_editor/ui/EditorToolbar.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <filesystem>
#include <string>

namespace engine::editor::world
{
	namespace
	{
		/// M100.4 — Parse le libellé persisté de `editor.world.camera.lastMode`
		/// vers l'enum. Convention : "FPS" / "Orbital" / "TopDown" (insensible
		/// à la casse, fallback FPS si valeur inconnue ou vide).
		EditorCameraMode ParseCameraModeString(const std::string& s)
		{
			if (s == "Orbital" || s == "orbital" || s == "ORBITAL")
				return EditorCameraMode::Orbital;
			if (s == "TopDown" || s == "topdown" || s == "TOPDOWN" || s == "TopDownOrtho")
				return EditorCameraMode::TopDownOrtho;
			return EditorCameraMode::FPS;
		}

		/// M100.4 — Inverse de `ParseCameraModeString` pour la persistance.
		const char* CameraModeToString(EditorCameraMode mode)
		{
			switch (mode)
			{
				case EditorCameraMode::FPS:          return "FPS";
				case EditorCameraMode::Orbital:      return "Orbital";
				case EditorCameraMode::TopDownOrtho: return "TopDown";
			}
			return "FPS";
		}
	}

	/// Initialise la coquille : lit `editor.world.layout_path`, instancie les
	/// 9 panneaux dans l'ordre stable, charge le fichier .ini de layout s'il
	/// existe, sinon réinitialise un layout par défaut. L'ordre des panneaux
	/// est figé : 0=Scene, 1=Inspector, 2=AssetBrowser, 3=Outliner, 4=Console,
	/// 5=ToolProperties, 6=History (M100.2), 7=SurfaceTable (M100.11),
	/// 8=CollisionEditor (M100.12) — référencé par les tests M100.1.
	bool WorldEditorShell::Init(const engine::core::Config& cfg)
	{
		m_layoutPath = cfg.GetString("editor.world.layout_path", "editor_world_layout.ini");

		// M100.2 — Configure la pile undo/redo depuis config.json. Defaults :
		// 256 commandes, 256 MiB. Le cast est sûr : capacity et maxBytes sont
		// stockés en size_t mais Config::GetInt renvoie int64_t.
		CommandStackConfig csCfg;
		csCfg.capacity = static_cast<size_t>(
			cfg.GetInt("editor.world.undo.capacity", 256));
		csCfg.maxBytes = static_cast<size_t>(
			cfg.GetInt("editor.world.undo.maxBytes", 256ll * 1024ll * 1024ll));
		m_commandStack.Configure(csCfg);

		m_panels.clear();
		m_panels.emplace_back(std::make_unique<panels::ScenePanel>());
		m_panels.emplace_back(std::make_unique<panels::InspectorPanel>());
		m_panels.emplace_back(std::make_unique<panels::AssetBrowserPanel>());
		m_panels.emplace_back(std::make_unique<panels::OutlinerPanel>());
		m_panels.emplace_back(std::make_unique<panels::ConsolePanel>());
		m_panels.emplace_back(std::make_unique<panels::ToolPropertiesPanel>());
		// M100.2 — Insère HistoryPanel après les 6 panneaux M100.1. L'ordre
		// devient : [Scene=0, Inspector=1, AssetBrowser=2, Outliner=3,
		//            Console=4, ToolProperties=5, History=6].
		m_panels.emplace_back(std::make_unique<panels::HistoryPanel>(&m_commandStack));
		// M100.11 — Panel lecture seule de la table de surfaces. Caché par
		// défaut, toggle via View > Surface Table.
		auto surfacePanel = std::make_unique<panels::SurfaceTablePanel>();
		surfacePanel->LoadFromContentRoot(
			std::filesystem::path(cfg.GetString("paths.content", "game/data")));
		m_panels.emplace_back(std::move(surfacePanel));
		// M100.12 — Panel d'authoring de collision proxies. Hidden par défaut,
		// toggle via View > Collision Editor.
		auto collisionPanel = std::make_unique<panels::CollisionEditorPanel>();
		collisionPanel->Init(
			std::filesystem::path(cfg.GetString("paths.content", "game/data")));
		m_panels.emplace_back(std::move(collisionPanel));

#if defined(_WIN32)
		std::error_code ec;
		if (std::filesystem::exists(m_layoutPath, ec) && !ec)
		{
			ImGui::LoadIniSettingsFromDisk(m_layoutPath.c_str());
		}
		else
		{
			ResetLayoutToDefault();
		}
#else
		ResetLayoutToDefault();
#endif

		// M100.4 — Restaure le mode caméra persisté entre sessions. Convention :
		// "FPS" / "Orbital" / "TopDown" (cf. spec M100.4 critère "Le mode actif
		// est persisté entre sessions"). La Config n'a pas (encore) de Save sur
		// disque ; cette lecture suffit dès qu'un autre composant écrit la clé
		// dans le JSON. La symétrie côté Shutdown est `SetValue` en mémoire —
		// la persistance disque sera complétée dans un follow-up M100.4 quand
		// `engine::core::Config` exposera un `SaveToFile` (TODO ci-dessous).
		const std::string lastMode = cfg.GetString("editor.world.camera.lastMode", "FPS");
		if (panels::ScenePanel* scene = GetScenePanel())
		{
			scene->MutableCamera().SetMode(ParseCameraModeString(lastMode));
		}

		// M100.6 — Branche l'outil de sculpture sur la pile undo + le doc
		// terrain partagés. Pas de chargement de chunks ici (lazy via
		// EnsureLoaded).
		m_sculptTool.Init(m_commandStack, m_terrainDoc);

		// M100.7 — Branche l'outil de stamp sur les mêmes ressources. Idem
		// sculpt : aucun chunk préchargé, c'est `OnClickAt` qui les charge à
		// la demande via `EnsureLoaded`.
		m_stampTool.Init(m_commandStack, m_terrainDoc);

		// M100.10 — Branche l'outil splat paint sur les mêmes ressources.
		// Aucun chunk préchargé : `OnMouseDown`/`ApplyAutoRulesToChunk`
		// font les `EnsureLoaded` + `EnsureSplatLoaded` à la demande.
		if (!m_splatPaintTool.Init(m_commandStack, m_terrainDoc))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] SplatPaintTool init failed");
		}

		// M100.13 — Init des outils Water (Lake + River) et chargement initial
		// du WaterDocument depuis instances/water.bin. LoadFromDisk retourne
		// true silencieusement si le fichier n'existe pas (premier lancement).
		m_lakeTool.Init(m_commandStack, m_waterDoc);
		m_riverTool.Init(m_commandStack, m_waterDoc, m_terrainDoc, cfg);
		std::string waterErr;
		if (!m_waterDoc.LoadFromDisk(cfg, waterErr))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] Water LoadFromDisk failed: {}", waterErr);
		}

		// M100.35 — Init des outils macro terrain (Mountain Range + Valley
		// Chain). Aucune pré-allocation de polyline ; l'utilisateur démarre
		// avec un état vide à chaque sélection d'outil. La Config est
		// mémorisée par chaque outil pour `EnsureLoaded` des chunks
		// impactés au moment de `Apply` (cf. RiverTool, même pattern).
		m_mountainRangeTool.Init(m_commandStack, m_terrainDoc, cfg);
		m_valleyChainTool.Init(m_commandStack, m_terrainDoc, cfg);

		// M100.36 — Init de l'outil River Network. Lit `OceanSettings` du
		// WaterDoc et initialise le buffer du slider sea level. Source du sea
		// level pour la sim : `m_waterDoc.GetOcean().seaLevelMeters` (jamais
		// un buffer local).
		m_riverNetworkTool.Init(m_commandStack, m_terrainDoc, m_waterDoc, cfg);

		// M100.37 — Init de l'outil Coastline. Partage la même `OceanSettings`
		// que River Network. Buffer local initialisé depuis le document.
		m_coastlineEditorTool.Init(m_commandStack, m_terrainDoc, m_waterDoc, cfg);

		// M100.6 — Injecte la référence au shell dans le ToolPropertiesPanel
		// (index 5, ordre stable garanti par l'init ci-dessus). Le panel s'en
		// sert pour lire `GetActiveTool()` et muter `MutableSculptTool()`.
		if (m_panels.size() > 5 && m_panels[5])
		{
			static_cast<panels::ToolPropertiesPanel*>(m_panels[5].get())->SetShell(this);
		}

		m_initialized = true;
		LOG_INFO(EditorWorld, "WorldEditorShell init OK, {} panels, layout='{}', cameraMode='{}'",
			m_panels.size(), m_layoutPath, lastMode);
		return true;
	}

	/// M100.6 — Active un outil et logge la transition. Garde l'API simple :
	/// pas de notification cross-outils (à compléter si les outils gagnent un
	/// cycle de vie OnEnter/OnExit).
	void WorldEditorShell::SetActiveTool(ActiveTool tool)
	{
		if (m_activeTool == tool) return;
		const ActiveTool prev = m_activeTool;
		m_activeTool = tool;
		const char* name = "None";
		switch (tool)
		{
			case ActiveTool::None:          name = "None"; break;
			case ActiveTool::TerrainSculpt: name = "TerrainSculpt"; break;
			case ActiveTool::TerrainStamp:  name = "TerrainStamp"; break;
			case ActiveTool::SplatPaint:    name = "SplatPaint"; break;
			case ActiveTool::Lake:          name = "Lake"; break;
			case ActiveTool::River:         name = "River"; break;
			case ActiveTool::MountainRange: name = "MountainRange"; break;
			case ActiveTool::ValleyChain:   name = "ValleyChain"; break;
			case ActiveTool::RiverNetwork:  name = "RiverNetwork"; break;
			case ActiveTool::Coastline:     name = "Coastline"; break;
		}
		(void)prev;
		LOG_INFO(EditorWorld, "Active tool -> {}", name);
	}

	/// Persiste le layout sur disque puis libère les panneaux. Idempotent.
	/// M100.4 — Logge le mode caméra final pour traçabilité ; la persistance
	/// disque de `editor.world.camera.lastMode` est différée à un follow-up
	/// quand `engine::core::Config` exposera un `SaveToFile`. TODO M100.4
	/// follow-up : appeler `cfg.SetValue("editor.world.camera.lastMode", …)`
	/// + `cfg.SaveToFile(...)`. En attendant, le mode est lu à chaque Init
	/// depuis la valeur déjà présente dans le JSON (modifiable à la main ou
	/// par un autre composant qui écrirait le fichier de config).
	void WorldEditorShell::Shutdown()
	{
		if (!m_initialized) return;
		const char* finalMode = "FPS";
		if (panels::ScenePanel* scene = GetScenePanel())
		{
			finalMode = CameraModeToString(scene->GetCamera().GetMode());
		}
		EnsureLayoutPersisted();
		m_panels.clear();
		m_initialized = false;
		LOG_INFO(EditorWorld, "WorldEditorShell shutdown (cameraMode='{}', persistance disque differee)",
			finalMode);
	}

	/// Sauvegarde l'état dock courant d'ImGui dans `m_layoutPath`.
	/// Effet de bord : écriture fichier disque.
	void WorldEditorShell::EnsureLayoutPersisted()
	{
#if defined(_WIN32)
		if (m_layoutPath.empty()) return;
		ImGui::SaveIniSettingsToDisk(m_layoutPath.c_str());
#endif
	}

	/// Rend le menu bar, le dockspace, puis chaque panneau visible. Doit
	/// être appelé après ImGui::NewFrame et avant ImGui::Render. Si Init n'a
	/// pas été appelé avec succès, no-op.
	void WorldEditorShell::RenderFrame()
	{
		if (!m_initialized) return;
#if defined(_WIN32)
		RenderMenuBar();
		// M100.35 — Toolbar à icônes rendue juste sous le menu bar, au-dessus
		// du dockspace. La fenêtre est non-dockable, fixée en haut, hauteur
		// 48 px. Elle remplace la `BeginTabBar("OutilsTabs")` historique de
		// `WorldEditorImGui::Render` (laquelle est supprimée par le même
		// ticket). Pas de mutation de la caméra ni du frustum cull.
		{
			EditorToolbar toolbar(*this);
			toolbar.Render();
		}
		RenderDockspace();
		for (auto& panel : m_panels)
		{
			if (panel && panel->IsVisible())
			{
				panel->Render();
			}
		}
#endif
	}

	/// Rend la barre de menu File/Edit/View/Tools/Window/Help. M100.1 : la
	/// plupart des items sont des stubs no-op qui loguent leur déclenchement.
	/// File/New/Open/Save sont stubs jusqu'aux tickets save/load. Edit/Undo
	/// /Redo grisés (activés en M100.2). View propose le toggle de chaque
	/// panneau et Reset Layout. Tools vide. Window expose 4 layouts pré-
	/// définis (3 identiques au Default en M100.1). Help/About : version
	/// figée pour l'instant.
	void WorldEditorShell::RenderMenuBar()
	{
#if defined(_WIN32)
		if (!ImGui::BeginMainMenuBar()) return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New", nullptr, false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/New (no-op M100.1, save/load à venir)");
			}
			if (ImGui::MenuItem("Open", nullptr, false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/Open (no-op M100.1, save/load à venir)");
			}
			if (ImGui::MenuItem("Save", "Ctrl+S", false, true))
			{
				MarkDirty("File/Save no-op M100.1");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Ctrl+Q", false, true))
			{
				LOG_INFO(EditorWorld, "Menu File/Exit (no-op M100.1, dialog à venir)");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			// M100.2 — Undo/Redo branchés sur le CommandStack. Les items sont
			// grisés quand la pile correspondante est vide.
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_commandStack.CanUndo()))
			{
				m_commandStack.Undo();
			}
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_commandStack.CanRedo()))
			{
				m_commandStack.Redo();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			for (auto& panel : m_panels)
			{
				if (!panel) continue;
				bool visible = panel->IsVisible();
				if (ImGui::MenuItem(panel->GetName(), nullptr, &visible))
				{
					panel->SetVisible(visible);
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu View/Reset Layout");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools"))
		{
			ImGui::TextDisabled("(à remplir par les tickets outils)");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			// Les 3 derniers layouts sont identiques au Default en M100.1.
			if (ImGui::MenuItem("Default Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Default Layout");
			}
			if (ImGui::MenuItem("Sculpting Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Sculpting Layout (alias Default M100.1)");
			}
			if (ImGui::MenuItem("Painting Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Painting Layout (alias Default M100.1)");
			}
			if (ImGui::MenuItem("Placement Layout"))
			{
				ResetLayoutToDefault();
				LOG_INFO(EditorWorld, "Menu Window/Placement Layout (alias Default M100.1)");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::Text("LCDLLN World Editor — M100.1 bootstrap");
			ImGui::Separator();
			ImGui::TextDisabled("Version + commit hash : à intégrer dans un ticket dédié.");
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
#endif
	}

	/// Crée le dockspace plein écran qui héberge tous les panneaux. Utilise
	/// le viewport principal d'ImGui.
	void WorldEditorShell::RenderDockspace()
	{
#if defined(_WIN32)
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("WorldEditorDockSpace", nullptr, flags);
		ImGui::PopStyleVar(3);
		const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceId");
		ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::End();
#endif
	}

	/// Mapping VK_F1..VK_F4, F6, F12 → panel index dans m_panels (ordre
	/// stable Scene/Inspector/AssetBrowser/Outliner/Console/ToolProperties).
	/// F5 (playtest) et F11 (fullscreen) sont consommés mais no-op M100.1.
	bool WorldEditorShell::HandleShortcut(int virtualKey)
	{
		// M100.4 — Numpad 1/3/7 : commute le mode caméra du ScenePanel.
		// Les VK_NUMPAD* codes sont VK_NUMPAD0=0x60..VK_NUMPAD9=0x69. On
		// traite ces touches AVANT le switch panel-index pour qu'elles ne
		// soient pas considérées comme inconnues. Si GetScenePanel() est
		// nullptr (Init pas encore appelé), on consomme la touche tout de
		// même : le shortcut ne ferait rien d'utile dans ce cas.
		if (virtualKey == 0x61 /*VK_NUMPAD1*/ ||
			virtualKey == 0x63 /*VK_NUMPAD3*/ ||
			virtualKey == 0x67 /*VK_NUMPAD7*/)
		{
			if (panels::ScenePanel* scene = GetScenePanel())
			{
				EditorCameraMode mode = EditorCameraMode::FPS;
				switch (virtualKey)
				{
					case 0x61: mode = EditorCameraMode::FPS; break;
					case 0x63: mode = EditorCameraMode::Orbital; break;
					case 0x67: mode = EditorCameraMode::TopDownOrtho; break;
				}
				scene->MutableCamera().SetMode(mode);
				LOG_INFO(EditorWorld, "Shortcut Numpad{} -> SetMode({})",
					(virtualKey == 0x61 ? "1" : virtualKey == 0x63 ? "3" : "7"),
					CameraModeToString(mode));
			}
			return true;
		}

		int panelIndex = -1;
		switch (virtualKey)
		{
			case 0x70: panelIndex = 0; break;  // VK_F1  → Scene
			case 0x71: panelIndex = 1; break;  // VK_F2  → Inspector
			case 0x72: panelIndex = 2; break;  // VK_F3  → Asset Browser
			case 0x73: panelIndex = 3; break;  // VK_F4  → Outliner
			case 0x75: panelIndex = 4; break;  // VK_F6  → Console
			case 0x7B: panelIndex = 5; break;  // VK_F12 → Tool Properties
			case 0x74:                          // VK_F5  → Réservé Playtest M100.33
				LOG_INFO(EditorWorld, "F5 (playtest) — réservé M100.33, no-op");
				return true;
			case 0x7A:                          // VK_F11 → Toggle plein écran
				LOG_INFO(EditorWorld, "F11 (fullscreen toggle) — no-op M100.1");
				return true;
			default:
				return false;
		}

		if (panelIndex >= 0 && panelIndex < static_cast<int>(m_panels.size()))
		{
			m_panels[panelIndex]->SetVisible(true);
#if defined(_WIN32)
			ImGui::SetWindowFocus(m_panels[panelIndex]->GetName());
#endif
			return true;
		}
		return false;
	}

	/// M100.2 — Surcharge avec modifiers : intercepte Ctrl+Z, Ctrl+Shift+Z,
	/// Ctrl+Y avant de déléguer à la version sans modifiers (F1..F12).
	/// Logge le déclenchement undo/redo en EditorWorld pour faciliter le
	/// debug "où est passée mon undo ?". 'Z' et 'Y' sont les codes ASCII
	/// majuscule, qui correspondent aux VK_* Win32 standards.
	bool WorldEditorShell::HandleShortcut(int virtualKey, bool ctrl, bool shift)
	{
		if (ctrl && virtualKey == 'Z' && !shift)
		{
			m_commandStack.Undo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Z -> Undo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		if (ctrl && virtualKey == 'Z' && shift)
		{
			m_commandStack.Redo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Shift+Z -> Redo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		if (ctrl && virtualKey == 'Y')
		{
			m_commandStack.Redo();
			LOG_INFO(EditorWorld, "Shortcut Ctrl+Y -> Redo (undoSize={}, redoSize={})",
				m_commandStack.UndoSize(), m_commandStack.RedoSize());
			return true;
		}
		// M100.6 — Raccourci 'B' (sans modifiers) active la sculpture terrain.
		// Spec ticket : "Tools → Terrain Sculpt ou raccourci B (brush)".
		if (!ctrl && !shift && virtualKey == 'B')
		{
			SetActiveTool(ActiveTool::TerrainSculpt);
			return true;
		}
		// M100.7 — Raccourci 'N' (sans modifiers) active l'outil stamp.
		// Spec ticket : "Activer outil via N".
		if (!ctrl && !shift && virtualKey == 'N')
		{
			SetActiveTool(ActiveTool::TerrainStamp);
			return true;
		}
		// M100.10 — Raccourci 'P' (sans modifiers) active l'outil splat paint.
		// Spec ticket : "Outil SplatPaintTool (raccourci P)".
		if (!ctrl && !shift && virtualKey == 'P')
		{
			SetActiveTool(ActiveTool::SplatPaint);
			return true;
		}
		// M100.13 — Raccourci 'L' (sans modifiers) active l'outil lac.
		if (!ctrl && !shift && virtualKey == 'L')
		{
			SetActiveTool(ActiveTool::Lake);
			return true;
		}
		// M100.13 — Raccourci 'R' (sans modifiers) active l'outil rivière.
		if (!ctrl && !shift && virtualKey == 'R')
		{
			SetActiveTool(ActiveTool::River);
			return true;
		}
		// M100.35 — Raccourcis optionnels (non documentés en UI principale)
		// pour activer les outils macros terrain. La spec impose Ctrl+Shift
		// pour éviter toute collision avec d'éventuels raccourcis 'M' / 'V'
		// simples qui pourraient être réservés à des futurs outils.
		if (ctrl && shift && virtualKey == 'M')
		{
			SetActiveTool(ActiveTool::MountainRange);
			return true;
		}
		if (ctrl && shift && virtualKey == 'V')
		{
			SetActiveTool(ActiveTool::ValleyChain);
			return true;
		}
		// M100.36 — Ctrl+Shift+N : River Network ("N" pour Network, évite la
		// collision avec 'R' (River simple, M100.13) et 'N' (TerrainStamp).
		if (ctrl && shift && virtualKey == 'N')
		{
			SetActiveTool(ActiveTool::RiverNetwork);
			return true;
		}
		// M100.37 — Ctrl+Shift+C : Coastline & Sea Level Editor.
		if (ctrl && shift && virtualKey == 'C')
		{
			SetActiveTool(ActiveTool::Coastline);
			return true;
		}
		return HandleShortcut(virtualKey);
	}

	/// Marque le document éditeur comme modifié et logge la raison.
	void WorldEditorShell::MarkDirty(std::string_view reason)
	{
		m_dirty = true;
		// std::format ne supporte pas string_view via {} directement avant C++23
		// pour certains compilers ; on convertit en std::string pour être large.
		LOG_INFO(EditorWorld, "WorldEditorShell dirty: {}", std::string(reason));
	}

	/// Réinitialise un layout par défaut minimal : rend tous les panneaux
	/// visibles. La disposition fine (Outliner | Scene | Inspector / Asset
	/// Browser | Tool Properties | Console) sera implémentée via ImGui
	/// DockBuilder dans un ticket d'intégration ultérieur — les tests
	/// M100.1 n'observent que la persistance du .ini (pas la disposition).
	void WorldEditorShell::ResetLayoutToDefault()
	{
		for (auto& panel : m_panels)
		{
			if (panel) panel->SetVisible(true);
		}
	}

	/// M100.4 — Retourne le ScenePanel via static_cast sur `m_panels[0]`
	/// (l'ordre des panneaux est figé par Init : Scene est toujours en 0).
	/// Utilise `static_cast` plutôt que `dynamic_cast` car le type concret
	/// est garanti par le code d'init qui pousse `std::make_unique<ScenePanel>`
	/// en premier — pas besoin du coût RTTI ni du fallback nullptr d'un mauvais cast.
	/// Retourne nullptr si Init n'a pas encore été appelé (m_panels vide).
	panels::ScenePanel* WorldEditorShell::GetScenePanel()
	{
		if (m_panels.empty() || !m_panels[0]) return nullptr;
		return static_cast<panels::ScenePanel*>(m_panels[0].get());
	}
}
