#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Tool Properties du shell éditeur monde (M100.1 — placeholder).
	/// Affichera les propriétés contextuelles de l'outil actif (sculpting,
	/// painting, placement) à mesure que les outils sont implémentés
	/// (M100.5+ : sculpting, M100.6+ : painting, M100.10+ : placement).
	class ToolPropertiesPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Tool Properties"; }

		/// Rend le placeholder texte du panneau Tool Properties.
		/// Effet de bord : crée une window ImGui nommée "Tool Properties".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
	};
}
