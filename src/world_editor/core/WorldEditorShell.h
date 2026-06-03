#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/erosion/HydraulicErosionTool.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionTool.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/arches/ArchTool.h"
#include "src/world_editor/volumes/caves/CaveTool.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"
#include "src/world_editor/volumes/overhangs/OverhangTool.h"
#include "src/world_editor/water/CoastlineEditorTool.h"
#include "src/world_editor/water/LakeTool.h"
#include "src/world_editor/water/RiverNetworkTool.h"
#include "src/world_editor/water/RiverTool.h"
#include "src/world_editor/splat/SplatPaintTool.h"
#include "src/world_editor/terrain/MountainRangeTool.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/TerrainSculptTool.h"
#include "src/world_editor/terrain/TerrainStampTool.h"
#include "src/world_editor/terrain/ValleyChainTool.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/world_editor/scene/EditorSceneModel.h" // sous-projet 1, bloc B (selection + scene)

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	namespace panels { class ScenePanel; }

	/// Identifiant de l'outil actif dans le shell éditeur monde (M100.6+).
	/// `None` est l'état initial après `Init`. `TerrainSculpt` est activé
	/// par le raccourci `B` (M100.6) ; `TerrainStamp` est activé par le
	/// raccourci `N` (M100.7) ; `SplatPaint` est activé par le raccourci `P`
	/// (M100.10). Les futurs outils (place…) s'ajouteront ici.
	enum class ActiveTool : uint8_t
	{
		None          = 0,
		TerrainSculpt = 1,
		TerrainStamp  = 2,
		SplatPaint    = 3,
		Lake          = 4,  // M100.13 — raccourci L
		River         = 5,  // M100.13 — raccourci R
		MountainRange    = 6,   // M100.35 — raccourci Ctrl+Shift+M
		ValleyChain      = 7,   // M100.35 — raccourci Ctrl+Shift+V
		RiverNetwork     = 8,   // M100.36 — raccourci Ctrl+Shift+N (network)
		Coastline           = 9,   // M100.37 — raccourci Ctrl+Shift+C
		HydraulicErosion    = 10,  // M100.38 — raccourci Ctrl+Shift+H
		ThermalWindErosion  = 11,  // M100.39 — raccourci Ctrl+Shift+T
		Cave                = 12,  // M100.40 — raccourci Ctrl+Shift+G (Grotte)
		Overhang            = 13,  // M100.41 — raccourci Ctrl+Shift+O (Overhang)
		Arch                = 14,  // M100.42 — raccourci Ctrl+Shift+A (Arche)
		DungeonPortal       = 15,  // M100.43 — raccourci Ctrl+Shift+D (Donjon)
	};

	/// Coquille principale de l'éditeur de monde 3D (M100.1). Instanciée une
	/// fois par processus quand `editor.world.enabled` est vrai (config.json)
	/// ou quand `--editor-world` est passé en CLI. Possède la liste des
	/// panneaux ancrables, le dockspace, le menu bar, et dispatche les
	/// raccourcis F1..F12 + Ctrl+Z/Y (Ctrl+Z/Y branchés en M100.2).
	///
	/// Vit en parallèle de WorldEditorImGui (mode "couche au-dessus") : les
	/// deux peuvent cohabiter dans le même processus, le routage des inputs
	/// est géré par Engine.cpp.
	///
	/// Contraintes thread/timing : toutes les méthodes publiques doivent être
	/// appelées depuis le main thread, ImGui n'étant pas thread-safe.
	class WorldEditorShell
	{
	public:
		/// Charge la config, instancie les panneaux, charge le layout ImGui.
		/// \param cfg Source des clés `editor.world.*` lues au démarrage.
		/// \return true si Init OK, false si layout_path non écrivable.
		/// Effet de bord : crée `editor_world_layout.ini` à Shutdown si absent.
		bool Init(const engine::core::Config& cfg);

		/// Persiste le layout ImGui sur disque puis libère les panneaux.
		/// Idempotent : ne fait rien si Init() n'a pas été appelé avec succès.
		void Shutdown();

		/// Appelée chaque frame depuis Engine::DrawFrame, après ImGui::NewFrame
		/// et avant la passe ImGui de rendu. Doit être appelée sur le main
		/// thread (ImGui n'est pas thread-safe).
		void RenderFrame();

		/// Dispatche F1..F12 vers le panneau correspondant. Le mapping est :
		/// F1=Scene, F2=Inspector, F3=Asset Browser, F4=Outliner, F5=playtest
		/// (no-op M100.1), F6=Console, F11=fullscreen (no-op M100.1),
		/// F12=Tool Properties.
		/// \param virtualKey VK_* Win32 (0x70..0x7B pour F1..F12).
		/// \return true si la touche a été consommée par le shell.
		bool HandleShortcut(int virtualKey);

		/// Surcharge avec modifiers Ctrl/Shift (M100.2). Branche les raccourcis
		/// éditeur au-dessus des touches « simples » F1..F12 :
		///   - Ctrl+Z (sans Shift) → `m_commandStack.Undo()`
		///   - Ctrl+Shift+Z         → `m_commandStack.Redo()`
		///   - Ctrl+Y               → `m_commandStack.Redo()`
		/// Si aucun de ces matchs ne s'applique, la fonction délègue à la
		/// surcharge à 1 argument (compat M100.1 : F1..F12).
		/// \param virtualKey VK_* Win32 (lettre majuscule pour 'Z'/'Y').
		/// \param ctrl true si Ctrl est tenu cette frame.
		/// \param shift true si Shift est tenu cette frame.
		/// \return true si la touche a été consommée par le shell.
		bool HandleShortcut(int virtualKey, bool ctrl, bool shift);

		/// Marque le document éditeur comme modifié.
		/// \param reason Texte court loggé en EditorWorld pour debug.
		/// Effet de bord : passe `m_dirty` à true et émet un LOG_INFO.
		void MarkDirty(std::string_view reason);

		/// Retourne true si MarkDirty a été appelé depuis le dernier "save"
		/// (le mécanisme de save sera ajouté dans un ticket ultérieur).
		bool IsDirty() const { return m_dirty; }

		/// Retourne true si Init() a été appelé avec succès et que Shutdown()
		/// n'a pas encore été appelé.
		bool IsInitialized() const { return m_initialized; }

		/// Accès lecture seule pour les tests et le HistoryPanel (M100.2).
		const std::vector<std::unique_ptr<IPanel>>& Panels() const { return m_panels; }
		std::vector<std::unique_ptr<IPanel>>&       MutablePanels()     { return m_panels; }

		/// Quand l'éditeur monde tourne dans le binaire `lcdlln_world_editor.exe`,
		/// la barre de menu M43.x (`WorldEditorImGui::BuildUi`) prend la main
		/// avec un menu français complet (incluant Zone Presets M100.46).
		/// Cette méthode supprime alors la barre M100.1 anglaise pour éviter
		/// la duplication visible — sans toucher au reste (panels, dockspace,
		/// shortcuts clavier). Appelée depuis `Engine.cpp` au boot.
		void SetMenuBarSuppressed(bool suppressed) { m_menuBarSuppressed = suppressed; }
		bool IsMenuBarSuppressed() const           { return m_menuBarSuppressed; }

		/// Accès mutable à la pile undo/redo (M100.2). Les outils concrets
		/// (sculpt, paint, place…) y poussent leurs `ICommand` via cet
		/// accesseur récupéré sur le shell partagé.
		CommandStack& MutableCommandStack() { return m_commandStack; }

		/// Accès lecture seule à la pile undo/redo (M100.2). Utilisé par les
		/// tests et les UIs en lecture (HistoryPanel passe par un pointeur
		/// non-const car il appelle Clear/RewindTo).
		const CommandStack& GetCommandStack() const { return m_commandStack; }

		/// M100.6 — Retourne l'outil actuellement actif (None par défaut).
		ActiveTool GetActiveTool() const { return m_activeTool; }

		/// M100.6 — Active un outil. La transition est immédiate ; aucun
		/// autre outil n'est notifié (à compléter quand plusieurs outils
		/// auront un cycle de vie partagé). Effet de bord : log info.
		void SetActiveTool(ActiveTool tool);

		/// M100.6 — Accès mutable au document terrain partagé. Les outils
		/// terrain (sculpt, stamps…) lisent/écrivent les chunks via ce
		/// document.
		TerrainDocument& MutableTerrainDocument() { return m_terrainDoc; }

		/// Sous-projet 1 (boucle d'edition d'une zone) — Initialise le terrain
		/// d'une zone neuve : alloue `chunksPerAxis × chunksPerAxis` chunks plats
		/// dans le `TerrainDocument` (source de verite). Les chunks sont marques
		/// dirty et seront ecrits au prochain `SaveTerrainChunks`. A appeler sur
		/// le main thread, depuis Engine, a la creation d'une nouvelle carte.
		/// \param chunksPerAxis empreinte d'edition (nombre de chunks par axe).
		void InitNewZoneTerrain(int chunksPerAxis);

		/// Sous-projet 1 — Persiste sur disque les chunks terrain + splat dirty
		/// (source de verite de la zone) sous `<paths.content>/chunks/`. Appelee
		/// depuis Engine quand l'utilisateur sauvegarde la carte.
		/// \param cfg source de `paths.content`.
		/// \return nombre total de fichiers chunk ecrits (terrain + splat).
		/// Effet de bord : ecriture disque.
		size_t SaveTerrainChunks(const engine::core::Config& cfg);

		/// Sous-projet 1, bloc B/C — Accès à l'état de sélection partagé de
		/// l'éditeur (consommé par l'Outliner pour sélectionner, par l'Inspector
		/// pour afficher/éditer l'entité courante).
		engine::editor::scene::EditorSelection&       MutableSelection()       { return m_selection; }
		const engine::editor::scene::EditorSelection& GetSelection()     const { return m_selection; }

		/// Sous-projet 1, bloc B/C — Accès à la vue agrégée des entités de la
		/// zone. L'Engine la lie aux documents sources (layout = session,
		/// mesh/donjon = shell) et la reconstruit chaque frame avant le rendu
		/// des panneaux.
		engine::editor::scene::EditorSceneModel&       MutableSceneModel()       { return m_sceneModel; }
		const engine::editor::scene::EditorSceneModel& GetSceneModel()     const { return m_sceneModel; }

		/// Sous-projet 1, bloc D — Type du foncteur d'écriture de transform :
		/// applique un `EntityTransform` à l'entité `EntityId` dans le document
		/// concret. Installé par l'Engine (qui capture les documents mutables) ;
		/// consommé par l'Inspector pour construire des `SetEntityTransformCommand`.
		using TransformWriter = std::function<void(
			engine::editor::scene::EntityId, const engine::editor::scene::EntityTransform&)>;

		/// Installe le foncteur d'écriture de transform (appelé par l'Engine).
		void SetTransformWriter(TransformWriter w) { m_transformWriter = std::move(w); }

		/// M100.6 — Accès mutable à l'outil de sculpt. Le panneau Tool
		/// Properties l'utilise pour lire/écrire les paramètres de brosse
		/// quand `m_activeTool == TerrainSculpt`.
		TerrainSculptTool& MutableSculptTool() { return m_sculptTool; }

		/// M100.6 — Accès lecture seule à l'outil de sculpt (tests, UI).
		const TerrainSculptTool& GetSculptTool() const { return m_sculptTool; }

		/// M100.7 — Accès mutable à l'outil de stamp. Le panneau Tool
		/// Properties l'utilise pour lire/écrire les paramètres de stamp
		/// quand `m_activeTool == TerrainStamp`.
		TerrainStampTool& MutableStampTool() { return m_stampTool; }

		/// M100.7 — Accès lecture seule à l'outil de stamp (tests, UI).
		const TerrainStampTool& GetStampTool() const { return m_stampTool; }

		/// M100.10 — Accès mutable à l'outil de peinture splat. Le panneau
		/// Tool Properties l'utilise pour lire/écrire les paramètres de
		/// peinture quand `m_activeTool == SplatPaint`.
		SplatPaintTool& MutableSplatPaintTool() { return m_splatPaintTool; }

		/// M100.10 — Accès lecture seule à l'outil splat paint (tests, UI).
		const SplatPaintTool& GetSplatPaintTool() const { return m_splatPaintTool; }

		/// M100.13 — Accès mutable à l'outil lac. Le panneau Tool Properties
		/// l'utilise pour lire/écrire les paramètres quand `m_activeTool == Lake`.
		LakeTool&             MutableLakeTool()        { return m_lakeTool; }

		/// M100.13 — Accès lecture seule à l'outil lac (tests, UI).
		const LakeTool&       GetLakeTool()      const { return m_lakeTool; }

		/// M100.13 — Accès mutable à l'outil rivière. Le panneau Tool Properties
		/// l'utilise pour lire/écrire les paramètres quand `m_activeTool == River`.
		RiverTool&            MutableRiverTool()       { return m_riverTool; }

		/// M100.13 — Accès lecture seule à l'outil rivière (tests, UI).
		const RiverTool&      GetRiverTool()     const { return m_riverTool; }

		/// M100.13 — Accès mutable au document water partagé (lacs + rivières).
		WaterDocument&        MutableWaterDocument()       { return m_waterDoc; }

		/// M100.13 — Accès lecture seule au document water (tests, UI).
		const WaterDocument&  GetWaterDocument()     const { return m_waterDoc; }

		/// M100.35 — Accès mutable à l'outil Mountain Range (chaîne de
		/// montagnes par polyline). Le panneau Tool Properties l'utilise
		/// pour lire/écrire la polyline en cours quand `m_activeTool ==
		/// MountainRange`.
		MountainRangeTool&       MutableMountainRangeTool()       { return m_mountainRangeTool; }
		const MountainRangeTool& GetMountainRangeTool()     const { return m_mountainRangeTool; }

		/// M100.35 — Accès mutable à l'outil Valley Chain. Jumeau de
		/// `MountainRangeTool` en mode soustractif (vallées).
		ValleyChainTool&         MutableValleyChainTool()       { return m_valleyChainTool; }
		const ValleyChainTool&   GetValleyChainTool()     const { return m_valleyChainTool; }

		/// M100.36 — Accès mutable à l'outil River Network. Le panneau Tool
		/// Properties l'utilise pour gérer la liste des sources et les
		/// paramètres de simulation quand `m_activeTool == RiverNetwork`.
		RiverNetworkTool&        MutableRiverNetworkTool()       { return m_riverNetworkTool; }
		const RiverNetworkTool&  GetRiverNetworkTool()     const { return m_riverNetworkTool; }

		/// M100.37 — Accès mutable à l'outil Coastline (sea level + ocean
		/// generation + smoothing/cliffs). Lit/écrit `WaterDocument::OceanSettings`
		/// via les mêmes accesseurs que River Network.
		CoastlineEditorTool&       MutableCoastlineEditorTool()       { return m_coastlineEditorTool; }
		const CoastlineEditorTool& GetCoastlineEditorTool()     const { return m_coastlineEditorTool; }

		/// M100.38 — Accès mutable à l'outil Hydraulic Erosion. Lit le sea
		/// level via `WaterDocument::GetOcean().seaLevelMeters` pour la
		/// condition `stopUnderSeaLevel`.
		erosion::HydraulicErosionTool&       MutableHydraulicErosionTool()       { return m_hydraulicErosionTool; }
		const erosion::HydraulicErosionTool& GetHydraulicErosionTool()     const { return m_hydraulicErosionTool; }

		/// M100.39 — Accès mutable à l'outil Thermal/Wind Erosion (clôture
		/// la Phase 2.5 « Terrain naturaliste »).
		erosion::ThermalWindErosionTool&       MutableThermalWindErosionTool()       { return m_thermalWindErosionTool; }
		const erosion::ThermalWindErosionTool& GetThermalWindErosionTool()     const { return m_thermalWindErosionTool; }

		/// M100.40 — Accès mutable au document Mesh Inserts (volumes 3D
		/// Phase 11). Consommé par les outils Cave (M100.40), Overhang
		/// (M100.41), Arch (M100.42), Dungeon (M100.43).
		volumes::MeshInsertDocument&       MutableMeshInsertDocument()       { return m_meshInsertDoc; }
		const volumes::MeshInsertDocument& GetMeshInsertDocument()     const { return m_meshInsertDoc; }

		/// M100.40 — Accès mutable à l'outil Cave (placement de grottes
		/// depuis catalogue glTF).
		volumes::caves::CaveTool&       MutableCaveTool()       { return m_caveTool; }
		const volumes::caves::CaveTool& GetCaveTool()     const { return m_caveTool; }

		/// M100.41 — Accès mutable à l'outil Overhang (placement de
		/// surplombs rocheux contre des falaises).
		volumes::overhangs::OverhangTool&       MutableOverhangTool()       { return m_overhangTool; }
		const volumes::overhangs::OverhangTool& GetOverhangTool()     const { return m_overhangTool; }

		/// M100.42 — Accès mutable à l'outil Arch (placement d'arches
		/// naturelles à partir de deux pieds monde).
		volumes::arches::ArchTool&       MutableArchTool()       { return m_archTool; }
		const volumes::arches::ArchTool& GetArchTool()     const { return m_archTool; }

		/// M100.43 — Accès mutable au document Dungeon Portal (Phase 11).
		/// Distinct du MeshInsertDocument car porte des metadata gameplay
		/// (template id, difficulty range, level gating).
		volumes::dungeons::DungeonPortalDocument&       MutableDungeonPortalDocument()       { return m_dungeonPortalDoc; }
		const volumes::dungeons::DungeonPortalDocument& GetDungeonPortalDocument()     const { return m_dungeonPortalDoc; }

		/// M100.43 — Accès mutable à l'outil Dungeon Portal.
		volumes::dungeons::DungeonPortalTool&       MutableDungeonPortalTool()       { return m_dungeonPortalTool; }
		const volumes::dungeons::DungeonPortalTool& GetDungeonPortalTool()     const { return m_dungeonPortalTool; }

	private:
		/// Rend la barre de menu File/Edit/View/Tools/Window/Help (M100.1
		/// stubs pour la plupart des items). Effet de bord : ImGui state.
		void RenderMenuBar();

		/// Rend le dockspace plein écran qui héberge tous les panneaux.
		/// Effet de bord : ImGui::DockSpaceOverViewport-like.
		void RenderDockspace();

		/// Persiste le layout ImGui sur disque vers `m_layoutPath`. Appelée
		/// par Shutdown(). Effet de bord : écriture fichier disque.
		void EnsureLayoutPersisted();

		/// Réinitialise le layout par défaut décrit dans la spec M100.1
		/// (Outliner | Scene | Inspector / Asset Browser | Tool Properties |
		/// Console). Effet de bord : ImGui DockBuilder state + réinitialise
		/// la visibilité de tous les panneaux à true.
		void ResetLayoutToDefault();

		/// M100.4 — Helper privé : retourne le ScenePanel (index 0 dans
		/// `m_panels`, ordre stable garanti par Init). Utilisé par
		/// `HandleShortcut` pour brancher Numpad 1/3/7 → SetMode et par
		/// Init/Shutdown pour persister `editor.world.camera.lastMode`.
		/// Retourne nullptr si Init n'a pas été appelé (m_panels vide).
		panels::ScenePanel* GetScenePanel();

		std::vector<std::unique_ptr<IPanel>> m_panels;
		CommandStack m_commandStack;
		TerrainDocument m_terrainDoc;
		engine::editor::scene::EditorSelection  m_selection;  // sous-projet 1, bloc B
		engine::editor::scene::EditorSceneModel m_sceneModel; // sous-projet 1, bloc B
		TransformWriter m_transformWriter;                    // sous-projet 1, bloc D
		TerrainSculptTool m_sculptTool;
		TerrainStampTool m_stampTool;
		SplatPaintTool m_splatPaintTool;
		LakeTool       m_lakeTool;       // M100.13
		RiverTool      m_riverTool;      // M100.13
		WaterDocument  m_waterDoc;       // M100.13
		MountainRangeTool m_mountainRangeTool; // M100.35
		ValleyChainTool   m_valleyChainTool;   // M100.35
		RiverNetworkTool  m_riverNetworkTool;  // M100.36
		CoastlineEditorTool m_coastlineEditorTool; // M100.37
		erosion::HydraulicErosionTool m_hydraulicErosionTool; // M100.38
		erosion::ThermalWindErosionTool m_thermalWindErosionTool; // M100.39
		volumes::MeshInsertDocument m_meshInsertDoc;          // M100.40
		volumes::caves::CaveTool    m_caveTool;               // M100.40
		volumes::overhangs::OverhangTool m_overhangTool;      // M100.41
		volumes::arches::ArchTool   m_archTool;               // M100.42
		volumes::dungeons::DungeonPortalDocument m_dungeonPortalDoc;  // M100.43
		volumes::dungeons::DungeonPortalTool     m_dungeonPortalTool; // M100.43
		ActiveTool m_activeTool = ActiveTool::None;
		std::string m_layoutPath;
		bool m_dirty = false;
		bool m_initialized = false;
		bool m_menuBarSuppressed = false;
	};
}
