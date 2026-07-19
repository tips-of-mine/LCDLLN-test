#include "src/world_editor/core/WorldEditorShell.h"

#include "src/world_editor/camera/EditorCameraController.h"
#include "src/world_editor/panels/ScenePanel.h"
#include "src/world_editor/panels/InspectorPanel.h"
#include "src/world_editor/panels/AssetBrowserPanel.h"
#include "src/world_editor/panels/BuildingEditorPanel.h"
#include "src/world_editor/panels/QuestEditorPanel.h"
#include "src/world_editor/panels/OutlinerPanel.h"
#include "src/world_editor/panels/ConsolePanel.h"
#include "src/world_editor/panels/ToolPropertiesPanel.h"
#include "src/world_editor/panels/HistoryPanel.h"
#include "src/world_editor/panels/SurfaceTablePanel.h"
#include "src/world_editor/panels/ToolPalettePanel.h"
#include "src/world_editor/panels/CollisionEditorPanel.h"
#include "src/world_editor/routine/RoutineGraphPanel.h"
#include "src/world_editor/ui/EditorToolbar.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <algorithm> // std::sort — suppression multi par index décroissant (Roadmap-6)
#include <cctype>   // std::tolower — ids kebab-case des toggles de panneaux
#include <cstring>  // std::strcmp — filtrage du panneau « Scene » dans le menu View
#include <functional>
#include <utility>
#include <vector>

#include "src/world_editor/core/CompositeCommand.h"        // Roadmap-6 : gestes multi-sélection
#include "src/world_editor/modes/EditorModeRegistry.h"
#include "src/world_editor/scene/DeleteEntityCommand.h"    // lot 5
#include "src/world_editor/scene/DuplicateEntityCommand.h" // lot 5
#include "src/world_editor/prefs/UserPrefsStore.h"
#include "src/world_editor/presets/ToolPresetRegistry.h"
#include "src/world_editor/help/HelpContentStore.h"
#include "src/world_editor/zone_presets/WorldMapEditDocumentReset.h"
#include "src/world_editor/zone_presets/ZonePresetRegistry.h"

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
		// Sous-projet 1, bloc D — Inspector réel : affiche + édite (undoable) les
		// propriétés de l'entité sélectionnée. Reçoit la pile de commandes et le
		// foncteur d'écriture de transform (installé par l'Engine).
		m_panels.emplace_back(std::make_unique<panels::InspectorPanel>(
			&m_sceneModel, &m_selection, &m_commandStack, &m_transformWriter));
		// Asset Browser : charge `meshes/props/catalog.json` ; sa sélection
		// alimente le Building Editor. On garde un pointeur brut (la propriété
		// reste à m_panels) pour l'injecter plus bas.
		auto assetBrowser = std::make_unique<panels::AssetBrowserPanel>();
		assetBrowser->SetContentRoot(cfg.GetString("paths.content", "game/data"));
		panels::AssetBrowserPanel* assetBrowserPtr = assetBrowser.get();
		m_panels.emplace_back(std::move(assetBrowser));
		// Sous-projet 1, bloc C — Outliner réel : reçoit le modèle de scène + la
		// sélection partagés (possédés par le Shell). L'Engine lie le modèle aux
		// documents et le reconstruit chaque frame.
		m_panels.emplace_back(std::make_unique<panels::OutlinerPanel>(&m_sceneModel, &m_selection));
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
		// M101.4 — Panel nodal de routines. AJOUTÉ EN FIN pour ne pas décaler
		// les indices fixes (Scene=0, ToolProperties=5). Caché par défaut,
		// toggle via le menu View. Partage la pile undo/redo du shell.
		m_panels.emplace_back(std::make_unique<RoutineGraphPanel>(&m_commandStack));
		// Auberge éditable — Building Editor : compose des variantes (sauvées
		// dans buildings/templates/<type>.json) et pose des références sur la
		// carte (buildings.bin). AJOUTÉ EN FIN (n'affecte pas les indices fixes).
		{
			auto buildingEditor = std::make_unique<panels::BuildingEditorPanel>();
			buildingEditor->SetAssetBrowser(assetBrowserPtr);
			buildingEditor->SetLibrary(&m_buildingLibrary);
			buildingEditor->SetDocument(&m_buildingDoc);
			buildingEditor->SetContentRoot(cfg.GetString("paths.content", "game/data"));
			m_buildingEditorPtr = buildingEditor.get(); // pour l'aperçu 3D live (Engine)
			m_panels.emplace_back(std::move(buildingEditor));
		}
		// SP4 — Quest Editor : authoring des quêtes (charger/éditer/valider/
		// enregistrer via QuestEditIo). AJOUTÉ EN FIN (n'affecte pas les
		// indices fixes). Pas d'aperçu 3D (donnée pure).
		{
			auto questEditor = std::make_unique<panels::QuestEditorPanel>();
			questEditor->SetContentRoot(cfg.GetString("paths.content", "game/data"));
			questEditor->SetIo(&m_questEditIo);
			m_questEditorPtr = questEditor.get();
			m_panels.emplace_back(std::move(questEditor));
		}

		// Réorganisation UI 2026-07-17 (PR 2) — palette d'outils latérale
		// (remplace la rangée d'outils M100.35 de l'EditorToolbar, devenue
		// barre d'actions). Ajoutée en FIN de liste : les raccourcis F1..F12
		// et plusieurs branchements mappent des INDICES FIXES dans m_panels
		// (Scene=0 … ToolProperties=5) — ne jamais insérer avant.
		m_panels.emplace_back(std::make_unique<panels::ToolPalettePanel>(this));

#if defined(_WIN32)
		std::error_code ec;
		if (std::filesystem::exists(m_layoutPath, ec) && !ec)
		{
			// IMPORTANT : Init() s'execute TOT au boot (Engine cree le Shell
			// avant l'init Vulkan), AVANT que WorldEditorImGui::Init n'appelle
			// ImGui::CreateContext. Appeler ImGui::LoadIniSettingsFromDisk ici
			// dereferencerait le contexte ImGui global (GImGui) encore nul ->
			// SEH 0xC0000005. Ce bug etait masque tant que le fichier de layout
			// n'existait pas (le crash de shutdown empechait sa persistance) ; il
			// est devenu reproductible des la 2e ouverture une fois le shutdown
			// corrige. On DIFFERE donc la lecture au 1er RenderFrame, ou le
			// contexte ImGui est garanti vivant.
			m_pendingLayoutLoad = true;
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

		// M100.38 — Init de l'outil Hydraulic Erosion. Lit le sea level via
		// `WaterDocument::GetOcean()` pour le `stopUnderSeaLevel`.
		m_hydraulicErosionTool.Init(m_commandStack, m_terrainDoc, m_waterDoc, cfg);

		// M100.39 — Init de l'outil combiné Thermal + Wind Erosion (clôture
		// la Phase 2.5 du milestone M100).
		m_thermalWindErosionTool.Init(m_commandStack, m_terrainDoc, m_waterDoc, cfg);

		// M100.40 — Init du document Mesh Inserts (Phase 11 « Volumes 3D »)
		// + outil Cave. Charge `instances/mesh_inserts.bin` si présent.
		std::string meshInsertErr;
		if (!m_meshInsertDoc.LoadFromDisk(cfg, meshInsertErr))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] MeshInsert LoadFromDisk failed: {}",
				meshInsertErr);
		}
		m_caveTool.Init(m_commandStack, m_meshInsertDoc, m_terrainDoc, cfg);

		// M100.41 — Init de l'outil Overhang (réutilise le `MeshInsertDoc`
		// initialisé ci-dessus). Le tool charge son propre catalogue
		// `meshes/overhangs/catalog.json`.
		m_overhangTool.Init(m_commandStack, m_meshInsertDoc, cfg);

		// M100.42 — Init de l'outil Arch (catalogue `meshes/arches/catalog.json`).
		m_archTool.Init(m_commandStack, m_meshInsertDoc, cfg);

		// M100.43 — Init du document Dungeon Portal (Phase 11, persiste dans
		// `instances/dungeon_portals.bin` LCDP v1) + outil DungeonPortal.
		// Distinct du MeshInsertDocument car portail = donnée gameplay.
		std::string dungeonErr;
		if (!m_dungeonPortalDoc.LoadFromDisk(cfg, dungeonErr))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] DungeonPortal LoadFromDisk failed: {}",
				dungeonErr);
		}
		m_dungeonPortalTool.Init(m_commandStack, m_dungeonPortalDoc, cfg);

		// Auberge éditable — Init du document des bâtiments (persiste dans
		// `instances/zone_<id>/buildings.bin` LCBD v1). Charge l'existant si présent.
		std::string buildingErr;
		if (!m_buildingDoc.LoadFromDisk(cfg, buildingErr))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] Building LoadFromDisk failed: {}",
				buildingErr);
		}
		// Bibliothèque des types de bâtiments (buildings/templates/*.json) :
		// alimente le Building Editor (création de variantes) et la résolution.
		std::string buildingLibErr;
		if (!m_buildingLibrary.LoadFromContent(
				cfg.GetString("paths.content", "game/data"), buildingLibErr)
			&& !buildingLibErr.empty())
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] Building library: {}", buildingLibErr);
		}

		// M100.45 — Phase 12 « Accessibilité ». Charge les préférences
		// utilisateur (`editor/user_prefs.json` — créé avec les défauts au
		// premier lancement) + le catalogue de presets d'outils
		// (`editor/tool_presets/*.json`). Le registry de mode est aligné
		// sur la préférence chargée sans ré-écrire le fichier.
		{
			const std::string contentRoot = cfg.GetString("paths.content", "game/data");
			const bool prefsExisted =
				engine::editor::world::prefs::UserPrefsStore::Instance().Init(contentRoot);
			const size_t presetFiles =
				engine::editor::world::presets::ToolPresetRegistry::Instance()
					.LoadFromContentPath(contentRoot);
			engine::editor::world::modes::EditorModeRegistry::Instance()
				.SetCurrentModeSilent(
					engine::editor::world::prefs::UserPrefsStore::Instance().GetEditorMode());
			LOG_INFO(EditorWorld,
				"[WorldEditorShell] M100.45 prefs {} ({} fichiers de presets)",
				prefsExisted ? "chargées" : "créées (premier lancement)", presetFiles);

			// M100.46 — catalogue de zone presets (`editor/zone_presets/*.json`),
			// consommé par le dialog « Nouvelle zone depuis preset ».
			const size_t zonePresets =
				engine::editor::world::zone_presets::ZonePresetRegistry::Instance()
					.LoadFromContentPath(contentRoot);
			LOG_INFO(EditorWorld,
				"[WorldEditorShell] M100.46 {} zone preset(s) chargé(s)", zonePresets);

			// M100.47 incrément 1 — catalogue de tooltips
			// (`editor/tooltips/*.json`), consommé par RichTooltipWidget
			// (incrément 3) au survol des sliders dans les
			// ToolPropertiesPanel.
			const size_t tooltips =
				engine::editor::world::help::HelpContentStore::Instance()
					.LoadFromContentPath(contentRoot);
			LOG_INFO(EditorWorld,
				"[WorldEditorShell] M100.47 {} tooltip(s) chargé(s)", tooltips);
		}

		// M100.6 — Injecte la référence au shell dans le ToolPropertiesPanel
		// (index 5, ordre stable garanti par l'init ci-dessus). Le panel s'en
		// sert pour lire `GetActiveTool()` et muter `MutableSculptTool()`.
		if (m_panels.size() > 5 && m_panels[5])
		{
			static_cast<panels::ToolPropertiesPanel*>(m_panels[5].get())->SetShell(this);
		}

		// Polish UI 2026-07-17 — visibilité par défaut « simple au premier
		// regard » : seuls les panneaux du flux principal (Palette d'outils,
		// Outliner, Inspector, Tool Properties) démarrent visibles. Les
		// panneaux avancés restent accessibles via le menu Fenêtre (toggles
		// du registre d'actions) et sont pré-dockés par la disposition par
		// défaut pour apparaître au bon endroit à l'ouverture.
		{
			static constexpr const char* kHiddenByDefault[] = {
				"Asset Browser", "Console", "History", "Surface Table",
				"Collision Editor", "Building Editor", "Quest Editor",
				"Routines",
			};
			for (auto& panel : m_panels)
			{
				if (!panel) continue;
				for (const char* hidden : kHiddenByDefault)
				{
					if (std::strcmp(panel->GetName(), hidden) == 0)
					{
						panel->SetVisible(false);
						break;
					}
				}
			}
		}

		// Réorganisation UI 2026-07-17 — actions autonomes du shell (undo/
		// redo, historique, toggles panneaux). Les actions dépendant de la
		// session (save/load/export…) sont ajoutées par
		// `WorldEditorImGui::RegisterEditorActions`.
		RegisterShellActions();

		m_initialized = true;
		LOG_INFO(EditorWorld, "WorldEditorShell init OK, {} panels, layout='{}', cameraMode='{}'",
			m_panels.size(), m_layoutPath, lastMode);
		return true;
	}

	void WorldEditorShell::RegisterShellActions()
	{
		using actions::ActionCategory;
		using actions::EditorAction;

		// Helper local : construit + enregistre une action en une expression.
		auto add = [this](const char* id, const char* label, ActionCategory cat,
			const char* shortcut,
			std::function<bool()> enabled, std::function<bool()> checked,
			std::function<void()> execute)
		{
			EditorAction a;
			a.id = id;
			a.label = label;
			a.category = cat;
			a.shortcutText = (shortcut != nullptr) ? shortcut : "";
			a.enabled = std::move(enabled);
			a.checked = std::move(checked);
			a.execute = std::move(execute);
			(void)m_actions.Register(std::move(a));
		};

		add("edit.undo", "Annuler", ActionCategory::Edition, "Ctrl+Z",
			[this] { return m_commandStack.CanUndo(); }, nullptr,
			[this] { m_commandStack.Undo(); });
		add("edit.redo", "Rétablir", ActionCategory::Edition, "Ctrl+Y",
			[this] { return m_commandStack.CanRedo(); }, nullptr,
			[this] { m_commandStack.Redo(); });

		// Lot 5 (2026-07-18) — Dupliquer / Supprimer l'entité sélectionnée
		// (Outliner ou Ctrl+clic viewport). Undoables via la pile de commandes ;
		// grisées tant qu'aucune entité duplicable/supprimable n'est
		// sélectionnée (Terrain/Water exclus) ou que l'Engine n'a pas installé
		// les foncteurs d'édition (SetEntityEditOps).
		add("edit.duplicate", "Dupliquer la sélection", ActionCategory::Edition, "Ctrl+D",
			[this] { return CanEditSelectedEntity(); }, nullptr,
			[this] { DuplicateSelectedEntity(); });
		add("edit.delete", "Supprimer la sélection", ActionCategory::Edition, "Suppr",
			[this] { return CanEditSelectedEntity(); }, nullptr,
			[this] { DeleteSelectedEntity(); });

		// Roadmap-6 (2026-07-19) — Modes du gizmo de transformation viewport
		// (toggles exclusifs, cf. raccourcis E/T/C dans HandleShortcut).
		add("tool.gizmo-translate", "Gizmo : déplacer", ActionCategory::Outils, "E",
			nullptr,
			[this] { return m_sceneGizmoMode == SceneGizmoMode::Translate; },
			[this] { SetSceneGizmoMode(SceneGizmoMode::Translate); });
		add("tool.gizmo-rotate", "Gizmo : tourner", ActionCategory::Outils, "T",
			nullptr,
			[this] { return m_sceneGizmoMode == SceneGizmoMode::Rotate; },
			[this] { SetSceneGizmoMode(SceneGizmoMode::Rotate); });
		add("tool.gizmo-scale", "Gizmo : échelle", ActionCategory::Outils, "C",
			nullptr,
			[this] { return m_sceneGizmoMode == SceneGizmoMode::Scale; },
			[this] { SetSceneGizmoMode(SceneGizmoMode::Scale); });

		// Toggle de visibilité par panneau (menu « Fenêtre »). Le panneau
		// « Scene » est exclu (doublon de la vue 3D principale, cf. menu Vue
		// historique). Id kebab-case dérivé du nom : "Asset Browser" →
		// "window.panel.asset-browser". Les IPanel* capturés sont possédés
		// par `m_panels` et restent valides jusqu'à `Shutdown`.
		for (auto& panel : m_panels)
		{
			if (!panel) continue;
			const char* name = panel->GetName();
			if (std::strcmp(name, "Scene") == 0) continue;
			std::string id = "window.panel.";
			for (const char* c = name; *c != '\0'; ++c)
			{
				id.push_back(*c == ' ' ? '-'
					: static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));
			}
			IPanel* p = panel.get();
			add(id.c_str(), name, ActionCategory::Fenetre, nullptr,
				nullptr,
				[p] { return p->IsVisible(); },
				[p] { p->SetVisible(!p->IsVisible()); });

			// « Historique des annulations » : alias d'ouverture du panneau
			// History dans le menu Édition (convention UE : l'historique
			// d'annulation se trouve sous Edit).
			if (std::strcmp(name, "History") == 0)
			{
				add("edit.history", "Historique des annulations",
					ActionCategory::Edition, nullptr,
					nullptr,
					[p] { return p->IsVisible(); },
					[p] { p->SetVisible(!p->IsVisible()); });
			}
		}
	}

	namespace
	{
		/// Lot 5 / Roadmap-6 — true si le kind est éditable structurellement
		/// (duplicable/supprimable) : Terrain (entité implicite unique), Water
		/// (pas de transform simple) et None sont exclus.
		bool IsStructurallyEditableKind(engine::editor::scene::EntityKind kind)
		{
			using K = engine::editor::scene::EntityKind;
			return kind == K::LayoutInstance || kind == K::MeshInsert || kind == K::DungeonPortal;
		}

		/// Roadmap-6 — Extrait de la sélection la liste des entités éditables,
		/// dans l'ordre de sélection (sans doublon, garanti par EditorSelection).
		std::vector<engine::editor::scene::EntityId> EditableSelection(
			const engine::editor::scene::EditorSelection& sel)
		{
			std::vector<engine::editor::scene::EntityId> out;
			for (const engine::editor::scene::EntityId& id : sel.Items())
			{
				if (IsStructurallyEditableKind(id.kind)) out.push_back(id);
			}
			return out;
		}
	}

	/// Lot 5 (2026-07-18, multi Roadmap-6) — true si AU MOINS une entité de la
	/// sélection est duplicable/supprimable (foncteurs Engine installés).
	bool WorldEditorShell::CanEditSelectedEntity() const
	{
		if (!m_entityEditOps.IsInstalled()) return false;
		for (const engine::editor::scene::EntityId& id : m_selection.Items())
		{
			if (IsStructurallyEditableKind(id.kind)) return true;
		}
		return false;
	}

	/// Lot 5 (multi Roadmap-6) — Duplique toutes les entités éditables de la
	/// sélection. Une commande par entité, regroupées en CompositeCommand si
	/// plusieurs (une seule étape d'annulation). Les copies s'ajoutent en FIN
	/// de liste : aucun index existant n'est invalidé, la sélection reste sur
	/// les originaux.
	void WorldEditorShell::DuplicateSelectedEntity()
	{
		const std::vector<engine::editor::scene::EntityId> ids =
			m_entityEditOps.IsInstalled()
				? EditableSelection(m_selection)
				: std::vector<engine::editor::scene::EntityId>{};
		if (ids.empty())
		{
			LOG_INFO(EditorWorld, "Dupliquer : aucune entité duplicable sélectionnée");
			return;
		}
		if (ids.size() == 1u)
		{
			m_commandStack.Push(std::make_unique<DuplicateEntityCommand>(ids.front(), m_entityEditOps));
		}
		else
		{
			auto composite = std::make_unique<CompositeCommand>(
				"Dupliquer " + std::to_string(ids.size()) + " entités");
			for (const engine::editor::scene::EntityId& id : ids)
			{
				composite->AddChild(std::make_unique<DuplicateEntityCommand>(id, m_entityEditOps));
			}
			m_commandStack.Push(std::move(composite));
		}
		LOG_INFO(EditorWorld, "Dupliquer la sélection ({} entité(s))", ids.size());
	}

	/// Lot 5 (multi Roadmap-6) — Supprime toutes les entités éditables de la
	/// sélection, par index DÉCROISSANT au sein de chaque kind : la capture du
	/// 1er Execute d'une DeleteEntityCommand se fait par index de scène, et
	/// retirer d'abord les index hauts garantit que les index bas restent
	/// valides pour les commandes suivantes du composite. VIDE ensuite la
	/// sélection (les index des entités restantes ont glissé).
	void WorldEditorShell::DeleteSelectedEntity()
	{
		std::vector<engine::editor::scene::EntityId> ids =
			m_entityEditOps.IsInstalled()
				? EditableSelection(m_selection)
				: std::vector<engine::editor::scene::EntityId>{};
		if (ids.empty())
		{
			LOG_INFO(EditorWorld, "Supprimer : aucune entité supprimable sélectionnée");
			return;
		}
		std::sort(ids.begin(), ids.end(),
			[](const engine::editor::scene::EntityId& a, const engine::editor::scene::EntityId& b)
			{
				if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
				return a.index > b.index; // index décroissant au sein du kind
			});
		if (ids.size() == 1u)
		{
			m_commandStack.Push(std::make_unique<DeleteEntityCommand>(ids.front(), m_entityEditOps));
		}
		else
		{
			auto composite = std::make_unique<CompositeCommand>(
				"Supprimer " + std::to_string(ids.size()) + " entités");
			for (const engine::editor::scene::EntityId& id : ids)
			{
				composite->AddChild(std::make_unique<DeleteEntityCommand>(id, m_entityEditOps));
			}
			m_commandStack.Push(std::move(composite));
		}
		const size_t n = ids.size();
		m_selection.Clear();
		LOG_INFO(EditorWorld, "Supprimer la sélection ({} entité(s))", n);
	}

	/// Roadmap-6 — Change le mode du gizmo viewport (E/T/C, actions
	/// `tool.gizmo-*`). Idempotent, logge la transition.
	void WorldEditorShell::SetSceneGizmoMode(SceneGizmoMode mode)
	{
		if (m_sceneGizmoMode == mode) return;
		m_sceneGizmoMode = mode;
		const char* name = "Translate";
		switch (mode)
		{
			case SceneGizmoMode::Translate: name = "Translate"; break;
			case SceneGizmoMode::Rotate:    name = "Rotate"; break;
			case SceneGizmoMode::Scale:     name = "Scale"; break;
		}
		LOG_INFO(EditorWorld, "Gizmo viewport -> {}", name);
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
			case ActiveTool::MountainRange:    name = "MountainRange"; break;
			case ActiveTool::ValleyChain:      name = "ValleyChain"; break;
			case ActiveTool::RiverNetwork:     name = "RiverNetwork"; break;
			case ActiveTool::Coastline:           name = "Coastline"; break;
			case ActiveTool::HydraulicErosion:    name = "HydraulicErosion"; break;
			case ActiveTool::ThermalWindErosion:  name = "ThermalWindErosion"; break;
			case ActiveTool::Cave:                name = "Cave"; break;
			case ActiveTool::Overhang:            name = "Overhang"; break;
			case ActiveTool::Arch:                name = "Arch"; break;
			case ActiveTool::DungeonPortal:       name = "DungeonPortal"; break;
			case ActiveTool::Spline:              name = "Spline"; break;        // Roadmap-8
			case ActiveTool::GameplayZone:        name = "GameplayZone"; break;  // Roadmap-8
			case ActiveTool::Hazard:              name = "Hazard"; break;        // Roadmap-8
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
		// Garde anti-crash : SaveIniSettingsToDisk dereference le contexte ImGui
		// global (GImGui). Au teardown, WorldEditorImGui::Shutdown peut avoir
		// deja appele ImGui::DestroyContext -> contexte nul -> access violation
		// (SEH 0xC0000005 observe a chaque fermeture de l'editeur). On ne
		// persiste donc que si le contexte est encore vivant. La sequence de
		// shutdown appelle desormais Shell::Shutdown AVANT la destruction du
		// contexte ImGui pour que le layout soit reellement sauvegarde.
		if (ImGui::GetCurrentContext() == nullptr) return;
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
		// Lecture differee du layout dock persiste (cf. Init) : on attend le 1er
		// frame ou le contexte ImGui existe pour eviter le crash de boot a la 2e
		// ouverture (LoadIniSettingsFromDisk sur contexte nul).
		if (m_pendingLayoutLoad && ImGui::GetCurrentContext() != nullptr)
		{
			ImGui::LoadIniSettingsFromDisk(m_layoutPath.c_str());
			m_pendingLayoutLoad = false;
		}
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

	/// Rend la barre de menu de repli Edit/View/Help. Réorganisation UI
	/// 2026-07-17 : la barre M100.1 historique (File/Edit/View/Tools/Window/
	/// Help) est dégraissée de ses stubs no-op (File New/Open/Save/Exit),
	/// de son menu Tools vide et de ses 4 layouts Window aliasés sur
	/// Default. Ne restent que les menus fonctionnels : Edit (undo/redo),
	/// View (toggles panneaux + reset layout), Help (version). Cette barre
	/// ne sert que le mode shell « couche au-dessus » in-game : dans le
	/// binaire éditeur monde, elle est supprimée (SetMenuBarSuppressed) au
	/// profit du menu français complet de WorldEditorImGui.
	void WorldEditorShell::RenderMenuBar()
	{
#if defined(_WIN32)
		// M100.46/47 — Quand le binaire éditeur monde tourne, la barre M43.x
		// (WorldEditorImGui::BuildUi) affiche un menu français complet avec
		// Zone Presets / Imports / Sauvegarde. La barre M100.1 anglaise serait
		// donc dupliquée et perturberait l'utilisateur — Engine.cpp la
		// supprime via SetMenuBarSuppressed(true) au boot. Les fonctions
		// (panels, undo/redo, layout reset) sont migrées dans le menu français
		// par WorldEditorImGui.
		if (m_menuBarSuppressed) return;
		if (!ImGui::BeginMainMenuBar()) return;

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
				// La fenêtre « Scene » a été retirée (doublon de la vue 3D
				// principale, cf. ScenePanel::Render) : on ne propose plus son
				// toggle dans le menu View. Le panneau reste m_panels[0] (indices
				// figés), mais n'a plus de fenêtre à afficher/masquer.
				if (std::strcmp(panel->GetName(), "Scene") == 0) continue;
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
		// Lot 5 (2026-07-18) — Ctrl+D (sans Shift) : dupliquer la sélection.
		// Ctrl+Shift+D reste l'activation de l'outil Dungeon Portal (plus bas).
		if (ctrl && !shift && virtualKey == 'D')
		{
			DuplicateSelectedEntity();
			return true;
		}
		// Lot 5 — Suppr (VK_DELETE, sans modifiers) : supprimer la sélection.
		if (!ctrl && !shift && virtualKey == 0x2E)
		{
			DeleteSelectedEntity();
			return true;
		}
		// Roadmap-6 (2026-07-19) — E / T / C (sans modifiers) : mode du gizmo
		// viewport (dÉplacer / Tourner / éChelle). W est réservé à la caméra
		// (WASD/ZQSD) et R à l'outil rivière (M100.13), d'où ce trio.
		if (!ctrl && !shift && virtualKey == 'E')
		{
			SetSceneGizmoMode(SceneGizmoMode::Translate);
			return true;
		}
		if (!ctrl && !shift && virtualKey == 'T')
		{
			SetSceneGizmoMode(SceneGizmoMode::Rotate);
			return true;
		}
		if (!ctrl && !shift && virtualKey == 'C')
		{
			SetSceneGizmoMode(SceneGizmoMode::Scale);
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
		// M100.38 — Ctrl+Shift+H : Hydraulic Erosion.
		if (ctrl && shift && virtualKey == 'H')
		{
			SetActiveTool(ActiveTool::HydraulicErosion);
			return true;
		}
		// M100.39 — Ctrl+Shift+T : Thermal/Wind Erosion (clôt la Phase 2.5).
		if (ctrl && shift && virtualKey == 'T')
		{
			SetActiveTool(ActiveTool::ThermalWindErosion);
			return true;
		}
		// M100.40 — Ctrl+Shift+G : Cave (Grotte, démarre Phase 11).
		if (ctrl && shift && virtualKey == 'G')
		{
			SetActiveTool(ActiveTool::Cave);
			return true;
		}
		// M100.41 — Ctrl+Shift+O : Overhang (surplomb rocheux).
		if (ctrl && shift && virtualKey == 'O')
		{
			SetActiveTool(ActiveTool::Overhang);
			return true;
		}
		// M100.42 — Ctrl+Shift+A : Arch (arche naturelle).
		if (ctrl && shift && virtualKey == 'A')
		{
			SetActiveTool(ActiveTool::Arch);
			return true;
		}
		// M100.43 — Ctrl+Shift+D : Dungeon Portal (portail de donjon).
		if (ctrl && shift && virtualKey == 'D')
		{
			SetActiveTool(ActiveTool::DungeonPortal);
			return true;
		}
		// M100.45 (Partie C) — Ctrl+Shift+R : hot-reload des presets d'outils
		// depuis le disque. Réservé aux builds debug — permet d'itérer sur
		// les `tool_presets/*.json` sans relancer l'éditeur.
#if !defined(NDEBUG)
		if (ctrl && shift && virtualKey == 'R')
		{
			const size_t n = presets::ToolPresetRegistry::Instance().Reload();
			LOG_INFO(EditorWorld, "[WorldEditorShell] Hot-reload presets : {} fichier(s)", n);
			return true;
		}
#endif
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

	void WorldEditorShell::InitNewZoneTerrain(int chunksPerAxis, float flatHeightMeters)
	{
		// Lot B3 (correctif 2) — toute création de zone invalide l'historique
		// undo/redo : les commandes mémorisées référencent les chunks de la
		// carte précédente et rejoueraient ses deltas au Ctrl+Z. Idempotent
		// si ResetForZoneChange a déjà vidé la pile juste avant.
		m_commandStack.Clear();
		m_terrainDoc.InitFlatZone(chunksPerAxis, flatHeightMeters);
		LOG_INFO(EditorWorld,
			"[WorldEditorShell] Init new zone terrain: {}x{} flat chunks at {:.1f} m",
			chunksPerAxis, chunksPerAxis, flatHeightMeters);
	}

	void WorldEditorShell::PropagateZoneIdToDocuments(const std::string& zoneId)
	{
		m_terrainDoc.SetZoneId(zoneId);
		m_waterDoc.SetZoneId(zoneId);
		m_meshInsertDoc.SetZoneId(zoneId);
		m_dungeonPortalDoc.SetZoneId(zoneId);
		m_buildingDoc.SetZoneId(zoneId);
	}

	void WorldEditorShell::ResetForZoneChange(const std::string& zoneId)
	{
		// Correctif 2 — l'historique undo/redo appartient à la carte qui se
		// ferme : on le détruit avant toute autre opération.
		m_commandStack.Clear();
		// Correctif 1 — évince de la RAM les chunks/instances de la carte
		// précédente (sinon SyncWorldEditorHeightmapFromDocument réécrirait
		// la heightmap de la nouvelle carte avec les hauteurs de l'ancienne).
		zone_presets::ResetEditedZoneDocuments(
			m_terrainDoc, m_waterDoc, m_meshInsertDoc, m_dungeonPortalDoc);
		m_buildingDoc.Reset();
		// Roadmap-8 — les 3 documents des outils câblés suivent le même cycle
		// de vie que les autres : vidés à chaque changement de carte.
		m_splineDoc.Clear();
		m_zoneDoc.Clear();
		m_hazardDoc.Clear();
		// Correctif 4 — namespace disque de la nouvelle carte.
		PropagateZoneIdToDocuments(zoneId);
		LOG_INFO(EditorWorld,
			"[WorldEditorShell] Reset for zone change: zone='{}' (undo vide, documents reinitialises)",
			zoneId);
	}

	void WorldEditorShell::LoadZoneDocuments(const engine::core::Config& cfg)
	{
		std::string err;
		if (!m_waterDoc.LoadFromDisk(cfg, err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Water LoadFromDisk failed: {}", err);
		}
		// Le flag dirty du WaterDocument sert aussi de signal « rebuild GPU »
		// (Engine::Render) ; LoadFromDisk le remet à false, on le ré-arme pour
		// que la scène d'eau chargée s'affiche au prochain tick.
		m_waterDoc.MarkDirty();
		err.clear();
		if (!m_meshInsertDoc.LoadFromDisk(cfg, err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] MeshInsert LoadFromDisk failed: {}", err);
		}
		err.clear();
		if (!m_dungeonPortalDoc.LoadFromDisk(cfg, err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] DungeonPortal LoadFromDisk failed: {}", err);
		}
		err.clear();
		if (!m_buildingDoc.LoadFromDisk(cfg, err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Building LoadFromDisk failed: {}", err);
		}
		// Roadmap-8 (dette audit 7.2) — les documents splines/zones/hazards
		// sont enfin RELUS (avant : écriture morte, contenu perdu au reload).
		err.clear();
		if (!m_splineDoc.LoadFromDisk(ZoneInstancesPath(cfg, "splines.bin"), err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Spline LoadFromDisk failed: {}", err);
		}
		err.clear();
		if (!m_zoneDoc.LoadFromDisk(ZoneInstancesPath(cfg, "zones.bin"), err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Zone LoadFromDisk failed: {}", err);
		}
		err.clear();
		if (!m_hazardDoc.LoadFromDisk(ZoneInstancesPath(cfg, "hazards.bin"), err))
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Hazard LoadFromDisk failed: {}", err);
		}
		LOG_INFO(EditorWorld,
			"[WorldEditorShell] Loaded zone documents: {} lake(s)/{} river(s), {} mesh insert(s), {} portal(s), {} building(s), {} spline(s), {} zone(s), {} hazard(s)",
			m_waterDoc.Get().lakes.size(), m_waterDoc.Get().rivers.size(),
			m_meshInsertDoc.Size(), m_dungeonPortalDoc.Size(), m_buildingDoc.Size(),
			m_splineDoc.All().size(), m_zoneDoc.All().size(), m_hazardDoc.All().size());
	}

	/// Roadmap-8 — Chemin d'un fichier d'instances de la zone courante.
	/// Effet de bord : crée le dossier parent au besoin (écriture disque).
	std::string WorldEditorShell::ZoneInstancesPath(const engine::core::Config& cfg, const char* file) const
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::string& zoneId = m_terrainDoc.GetZoneId();
		std::string dir = contentRoot + "/instances";
		if (!zoneId.empty()) dir += "/zone_" + zoneId;
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir + "/" + file;
	}

	size_t WorldEditorShell::SaveZoneDocuments(const engine::core::Config& cfg)
	{
		size_t written = 0;
		std::string err;
		if (m_waterDoc.SaveToDisk(cfg, err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Water SaveToDisk failed: {}", err);
		}
		err.clear();
		if (m_meshInsertDoc.SaveToDisk(cfg, err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] MeshInsert SaveToDisk failed: {}", err);
		}
		err.clear();
		if (m_dungeonPortalDoc.SaveToDisk(cfg, err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] DungeonPortal SaveToDisk failed: {}", err);
		}
		err.clear();
		if (m_buildingDoc.SaveToDisk(cfg, err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Building SaveToDisk failed: {}", err);
		}
		// Roadmap-8 — persistance des 3 documents des outils câblés.
		err.clear();
		if (m_splineDoc.SaveToDisk(ZoneInstancesPath(cfg, "splines.bin"), err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Spline SaveToDisk failed: {}", err);
		}
		err.clear();
		if (m_zoneDoc.SaveToDisk(ZoneInstancesPath(cfg, "zones.bin"), err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Zone SaveToDisk failed: {}", err);
		}
		err.clear();
		if (m_hazardDoc.SaveToDisk(ZoneInstancesPath(cfg, "hazards.bin"), err)) { ++written; }
		else
		{
			LOG_WARN(EditorWorld,
				"[WorldEditorShell] Hazard SaveToDisk failed: {}", err);
		}
		LOG_INFO(EditorWorld,
			"[WorldEditorShell] Saved zone documents: {}/7 (eau, mesh inserts, portails, batiments, splines, zones, hazards)",
			written);
		return written;
	}

	size_t WorldEditorShell::SaveTerrainChunks(const engine::core::Config& cfg)
	{
		const size_t terrainWritten = m_terrainDoc.SaveDirtyToDisk(cfg);
		const size_t splatWritten   = m_terrainDoc.SaveDirtySplatToDisk(cfg);
		LOG_INFO(EditorWorld,
			"[WorldEditorShell] Saved terrain chunks: {} terrain + {} splat",
			terrainWritten, splatWritten);
		return terrainWritten + splatWritten;
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
