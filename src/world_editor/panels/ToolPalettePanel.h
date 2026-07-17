#pragma once
// ToolPalettePanel — palette d'outils latérale (réorganisation UI
// 2026-07-17, PR 2, convention UE « Modes »). Remplace la rangée d'outils
// horizontale M100.35 : les 15 outils du shell sont présentés en 4 familles
// repliables (Terrain / Eau / Macro / Structures — cf. ToolPaletteModel),
// avec l'outil actif surligné et un bouton « Aucun outil » en tête.

#include "src/world_editor/core/IPanel.h"

namespace engine::editor::world { class WorldEditorShell; }

namespace engine::editor::world::panels
{
	/// Panneau dockable « Palette d'outils » du shell éditeur monde.
	/// Docké à gauche dans la disposition par défaut (même node que la
	/// fenêtre « Outils » de la session M43.x). Un clic sur un outil appelle
	/// `WorldEditorShell::SetActiveTool` (clic sur l'outil déjà actif =
	/// désélection, même convention que le menu Outils).
	class ToolPalettePanel final : public IPanel
	{
	public:
		/// \param shell Shell propriétaire de l'outil actif. Pointeur non
		///   possédé, non nul, valide pendant toute la vie du panneau (le
		///   shell possède le panneau via `m_panels`).
		explicit ToolPalettePanel(WorldEditorShell* shell) : m_shell(shell) {}

		const char* GetName() const override { return "Palette d'outils"; }

		/// Rend les groupes repliables + boutons d'outils. L'outil actif a
		/// un fond ambre (même teinte que l'ex-toolbar M100.35), la pastille
		/// de couleur vient de `ToolbarIconAtlas`, le tooltip affiche le
		/// raccourci depuis le registre d'actions du shell.
		/// Effet de bord : crée une window ImGui « Palette d'outils » ;
		/// appelle `SetActiveTool` au clic.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		WorldEditorShell* m_shell = nullptr;
		bool m_visible = true;
	};
}
