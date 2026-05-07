#pragma once
#include "engine/editor/world/IPanel.h"
#include "engine/editor/world/StampLibrary.h"

#include <string>
#include <vector>

namespace engine::editor::world
{
	class WorldEditorShell;
}

namespace engine::editor::world::panels
{
	/// Panneau Tool Properties du shell éditeur monde (M100.1 → M100.7).
	/// M100.1 : placeholder uniquement.
	/// M100.6 : si l'outil actif est TerrainSculpt, rend les paramètres de
	///         brosse (mode radio, sliders radius/strength/falloff, sliders
	///         noise + checkboxes mirror).
	/// M100.7 : si l'outil actif est TerrainStamp, rend les paramètres de
	///         stamp (radio source library/procedural, combos, sliders
	///         footprint/strength/rotation, radio mode, boutons Apply/Cancel).
	/// La référence au shell est passée via `SetShell` après l'instanciation
	/// (le shell construit le panneau dans `m_panels` puis injecte son adresse).
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
		/// M100.7 — Recharge la liste des stamps depuis `m_stampLibraryDir`
		/// (par défaut `assets/editor/stamps`). Appelée à la demande via le
		/// bouton "Refresh", et automatiquement à la première ouverture du
		/// panneau Stamp si `m_stampLibraryLoaded` est false.
		/// Effet de bord : remplit `m_stampLibrary`.
		void RefreshStampLibrary();

		bool m_visible = true;
		WorldEditorShell* m_shell = nullptr;

		// M100.7 — Cache des entrées library + état UI
		std::string m_stampLibraryDir = "assets/editor/stamps";
		std::vector<engine::editor::world::StampEntry> m_stampLibrary;
		bool m_stampLibraryLoaded = false;
		int m_stampLibrarySelected = 0;
	};
}
