#pragma once
#include "src/world_editor/core/IPanel.h"

namespace engine::editor::world
{
	class WorldEditorShell;
}

namespace engine::editor::world::panels
{
	/// Panneau des 16 calques (Lot 0). Renommage, visibilité, verrou, couleur,
	/// et « assigner la sélection au calque ». Lit/écrit le `LayersDocument` et
	/// la sélection via le shell injecté. Rendu ImGui gardé `_WIN32`.
	class LayersPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Layers"; }

		/// Rend la liste des 16 calques + le bouton d'assignement. No-op hors
		/// Windows ou si le shell n'est pas lié. Effet de bord : window ImGui
		/// "Layers" + mutations du `LayersDocument` au clic.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Injecte le shell (durée de vie garantie : le shell possède le panneau).
		/// \param shell propriétaire du `LayersDocument` et de la sélection ; peut
		///        être nul tant que `Init` ne l'a pas appelé.
		void SetShell(WorldEditorShell* shell) { m_shell = shell; }

	private:
		bool m_visible = true;
		WorldEditorShell* m_shell = nullptr;
		int m_assignTargetLayer = 0; ///< Calque cible du bouton « assigner ».
	};
}
