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

		// --- Aperçu 3D live (consommé par Engine en mode éditeur) -----------
		/// Pièces du brouillon en cours de composition (espace local).
		const std::vector<engine::world::instances::BuildingPart>& DraftParts() const { return m_draftParts; }
		/// Origine monde où prévisualiser le brouillon = position de pose courante.
		float PreviewX() const { return m_placePos[0]; }
		float PreviewZ() const { return m_placePos[1]; }
		float PreviewYaw() const { return m_placeYaw; }
		float PreviewScale() const { return m_placeScale; }
		/// True une fois si l'aperçu doit être reconstruit (brouillon/doc changé,
		/// ou bouton « Rafraîchir »). Remet le flag à false (edge-triggered).
		bool ConsumePreviewDirty() { const bool d = m_previewDirty; m_previewDirty = false; return d; }
		/// Force un rebuild de l'aperçu au prochain tick (ex: zone chargée).
		void MarkPreviewDirty() { m_previewDirty = true; }

	private:
		bool m_visible = true;
		bool m_previewDirty = true; // aperçu 3D à (re)construire

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

		// Paramètres de pose sur la carte (référence). Défaut (0,0) = origine,
		// visible d'emblée dans la vue éditeur ; l'aperçu 3D du brouillon s'y
		// affiche (déplaçable via ces champs + « Rafraichir l'apercu »).
		float m_placePos[2]    = { 0.0f, 0.0f }; // X, Z monde
		float m_placeYaw       = 0.0f;
		float m_placeScale     = 1.0f;

		std::string m_status; // dernier message (succès/erreur), affiché en bas
	};
}
