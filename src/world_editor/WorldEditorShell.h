#pragma once
#include "src/world_editor/IPanel.h"
#include "src/world_editor/CommandStack.h"
#include "src/world_editor/LakeTool.h"
#include "src/world_editor/RiverTool.h"
#include "src/world_editor/SplatPaintTool.h"
#include "src/world_editor/TerrainDocument.h"
#include "src/world_editor/TerrainSculptTool.h"
#include "src/world_editor/TerrainStampTool.h"
#include "src/world_editor/WaterDocument.h"

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
		TerrainSculptTool m_sculptTool;
		TerrainStampTool m_stampTool;
		SplatPaintTool m_splatPaintTool;
		LakeTool       m_lakeTool;       // M100.13
		RiverTool      m_riverTool;      // M100.13
		WaterDocument  m_waterDoc;       // M100.13
		ActiveTool m_activeTool = ActiveTool::None;
		std::string m_layoutPath;
		bool m_dirty = false;
		bool m_initialized = false;
	};
}
