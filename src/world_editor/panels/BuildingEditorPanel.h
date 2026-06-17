#pragma once

#include "src/world_editor/core/IPanel.h"
#include "src/client/world/instances/BuildingTemplates.h"

#include <string>
#include <vector>

namespace engine::world::instances { class BuildingTemplateLibrary; }
namespace engine::editor::world::buildings { class BuildingDocument; }

namespace engine::editor::world::panels
{
	class AssetBrowserPanel;

	/// Panneau d'édition de bâtiments : compose une VARIANTE (grappe de pièces)
	/// à partir des assets sélectionnés dans l'Asset Browser, l'enregistre dans
	/// le fichier de son TYPE (`buildings/templates/<type>.json` via
	/// `BuildingTemplateLibrary::SaveVariant` — « à chaque création ça se
	/// sauvegarde dans le fichier du type »), puis pose une RÉFÉRENCE sur la
	/// carte (`BuildingDocument`, écrit dans `buildings.bin`). Workflow par
	/// formulaire (pas de raycast viewport dans ce MVP).
	///
	/// Dépendances injectées (non possédées) via les setters, à l'init du shell.
	class BuildingEditorPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Building Editor"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		void SetAssetBrowser(AssetBrowserPanel* b) { m_assetBrowser = b; }
		void SetLibrary(engine::world::instances::BuildingTemplateLibrary* l) { m_library = l; }
		void SetDocument(engine::editor::world::buildings::BuildingDocument* d) { m_doc = d; }
		void SetContentRoot(std::string root) { m_contentRoot = std::move(root); }

	private:
		bool m_visible = true;

		AssetBrowserPanel* m_assetBrowser = nullptr;
		engine::world::instances::BuildingTemplateLibrary* m_library = nullptr;
		engine::editor::world::buildings::BuildingDocument* m_doc = nullptr;
		std::string m_contentRoot = "game/data";

		// Variante en cours de composition.
		std::vector<engine::world::instances::BuildingPart> m_draftParts;
		char m_typeBuf[64]      = "tavern";
		char m_typeNameBuf[96]  = "Taverne / Auberge";
		char m_variantBuf[64]   = "";
		char m_variantNameBuf[96] = "";

		// Paramètres de la pièce à ajouter (transform local).
		float m_newPos[3]      = { 0.0f, 0.0f, 0.0f };
		float m_newYaw         = 0.0f;
		float m_newScale       = 1.0f;
		bool  m_newSolid       = true;
		float m_newCollision   = 0.0f;

		// Paramètres de pose sur la carte (référence).
		float m_placePos[2]    = { 88.0f, 100.0f }; // X, Z monde
		float m_placeYaw       = 0.0f;
		float m_placeScale     = 1.0f;

		std::string m_status; // dernier message (succès/erreur), affiché en bas
	};
}
