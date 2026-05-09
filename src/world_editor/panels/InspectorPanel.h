#pragma once
#include "src/world_editor/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Inspector du shell éditeur monde (M100.1 — placeholder).
	/// Affichera les propriétés de la sélection courante à partir de
	/// M100.34 (Selection / Layers).
	class InspectorPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Inspector"; }

		/// Rend le placeholder texte du panneau Inspector.
		/// Effet de bord : crée une window ImGui nommée "Inspector".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
	};
}
