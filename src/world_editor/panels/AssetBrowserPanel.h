#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Asset Browser du shell éditeur monde (M100.1 — placeholder).
	/// Affichera les ressources disponibles (meshes, textures, sons,
	/// matériaux) à partir des tickets de chargement assets dédiés.
	class AssetBrowserPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Asset Browser"; }

		/// Rend le placeholder texte du panneau Asset Browser.
		/// Effet de bord : crée une window ImGui nommée "Asset Browser".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
	};
}
