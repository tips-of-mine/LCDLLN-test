#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/assets/AssetCatalog.h"

#include <string>

namespace engine::editor::world::panels
{
	/// Panneau Asset Browser : liste les assets du kit (meshes/props) depuis
	/// `catalog.json`, groupés par catégorie, avec recherche. La sélection
	/// courante est exposée pour le BuildingEditorPanel (composition de
	/// variantes). Le catalogue est chargé paresseusement au premier rendu.
	class AssetBrowserPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Asset Browser"; }

		/// Rend la liste filtrable du catalogue. Effet de bord : crée une window
		/// ImGui "Asset Browser" ; charge le catalogue au 1er appel.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Définit la racine de contenu pour charger
		/// `<root>/meshes/props/catalog.json`. Réinitialise le chargement.
		void SetContentRoot(std::string root)
		{
			m_contentRoot = std::move(root);
			m_loaded = false;
		}

		/// Id de l'asset sélectionné ("" si aucun).
		const std::string& SelectedAssetId() const { return m_selectedId; }

		/// Entrée catalogue sélectionnée, ou nullptr si aucune.
		const assets::AssetCatalogEntry* SelectedAsset() const
		{
			return m_selectedId.empty() ? nullptr : m_catalog.FindById(m_selectedId);
		}

	private:
		/// Charge le catalogue si pas encore fait (idempotent). Pur (hors ImGui).
		void EnsureLoaded();

		bool m_visible = true;
		std::string m_contentRoot = "game/data";
		bool m_loaded = false;
		assets::AssetCatalog m_catalog;
		std::string m_selectedId;
		std::string m_categoryFilter; // "" = toutes catégories
		char m_search[64] = { 0 };
	};
}
