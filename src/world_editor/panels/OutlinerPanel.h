#pragma once
#include "src/world_editor/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Outliner du shell éditeur monde (M100.1 — placeholder).
	/// Affichera la hiérarchie des entités du monde (zones, props, lumières)
	/// à partir de M100.34 (Selection / Layers).
	class OutlinerPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Outliner"; }

		/// Rend le placeholder texte du panneau Outliner.
		/// Effet de bord : crée une window ImGui nommée "Outliner".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
	};
}
