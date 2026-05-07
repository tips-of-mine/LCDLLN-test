#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world
{
	class WorldEditorShell;
}

namespace engine::editor::world::panels
{
	/// Panneau Tool Properties du shell éditeur monde (M100.1 → M100.6).
	/// M100.1 : placeholder uniquement.
	/// M100.6 : si l'outil actif est TerrainSculpt, rend les paramètres de
	///         brosse (mode radio, sliders radius/strength/falloff, sliders
	///         noise + checkboxes mirror). La référence au shell est passée
	///         via `SetShell` après l'instanciation (le shell construit le
	///         panneau dans `m_panels` puis injecte son adresse).
	class ToolPropertiesPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Tool Properties"; }

		/// Rend le panneau Tool Properties. Si aucun outil n'est actif (ou
		/// si `m_shell` est null), affiche le placeholder M100.1.
		/// Effet de bord : crée une window ImGui nommée "Tool Properties".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// M100.6 — Injecte la référence au shell pour que le panneau puisse
		/// lire `GetActiveTool()` et muter les paramètres de la brosse via
		/// `MutableSculptTool()`. Doit être appelé après l'instanciation,
		/// idéalement depuis `WorldEditorShell::Init`. Le shell garantit la
		/// durée de vie : il possède le panneau et l'outil.
		void SetShell(WorldEditorShell* shell) { m_shell = shell; }

	private:
		bool m_visible = true;
		WorldEditorShell* m_shell = nullptr;
	};
}
