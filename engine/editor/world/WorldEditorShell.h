#pragma once
#include "engine/editor/world/IPanel.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
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

		std::vector<std::unique_ptr<IPanel>> m_panels;
		std::string m_layoutPath;
		bool m_dirty = false;
		bool m_initialized = false;
	};
}
