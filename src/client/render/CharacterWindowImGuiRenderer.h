#pragma once
// Fenêtre Personnage unifiée à onglets (Chantier 1). Regroupe perso 3D +
// inventaire + caractéristiques + argent (onglet défaut) et délègue aux
// renderers Compétences/Techniques/Arbre en mode embarqué. Rendu au même point
// de frame que les panneaux existants (bloc single-pass) -> pas de doublon.

#include <cstdint>

namespace engine::core { class Config; }
namespace engine::client { class UIModelBinding; class InventoryUiPresenter; class SkillIconCache; struct UIModel; }
namespace engine::render { class SkillBookImGuiRenderer; class GrimoireImGuiRenderer; class ClassSkillTreeImGuiRenderer; class ExploitsImGuiRenderer; }
namespace engine::render::race { class RacePreviewViewport; }
namespace engine::items { class ItemCatalog; }

namespace engine::render
{
	class CharacterWindowImGuiRenderer
	{
	public:
		enum class Tab { Personnage = 0, Competences, Techniques, Arbre, Exploits };

		/// Câble les dépendances (pointeurs non possédés). Les 3 renderers de
		/// panneaux sont pilotés en mode embarqué par cette fenêtre.
		void Bind(const engine::core::Config* cfg,
			const engine::client::UIModelBinding* uiBinding,
			const engine::client::InventoryUiPresenter* inv,
			engine::client::SkillIconCache* icons,
			engine::render::SkillBookImGuiRenderer* skillBook,
			engine::render::GrimoireImGuiRenderer* grimoire,
			engine::render::ClassSkillTreeImGuiRenderer* classTree);

		/// Chantier 2 SP-A — catalogue d'objets client (résolution itemId → nom/slot/
		/// bonus pour le panneau équipement et les tooltips enrichis). Nul -> noms bruts.
		void SetItemCatalog(const engine::items::ItemCatalog* catalog) { m_itemCatalog = catalog; }

		/// Chantier 2 SP-A — action d'équipement demandée par l'utilisateur au clic,
		/// drainée par Engine après Render() pour l'envoyer au serveur (le renderer
		/// n'a pas accès au socket).
		struct PendingEquipAction
		{
			enum class Kind { None, Equip, Unequip } kind = Kind::None;
			uint32_t itemId = 0; ///< pour Equip (objet du sac cliqué)
			uint8_t slot = 0;    ///< pour Unequip (slot cliqué, 1..10)
		};
		/// Récupère et efface l'action en attente. Retourne false si aucune.
		bool ConsumeEquipAction(PendingEquipAction& out)
		{
			if (m_pendingEquip.kind == PendingEquipAction::Kind::None) return false;
			out = m_pendingEquip;
			m_pendingEquip = PendingEquipAction{};
			return true;
		}

		/// SP2 anniversaires (2026-07-18) — renderer de l'onglet Exploits
		/// (non possédé). Nul -> onglet absent (rétro-compatible).
		void SetExploitsRenderer(engine::render::ExploitsImGuiRenderer* r) { m_exploits = r; }

		/// Viewport 3D du perso (optionnel ; Task 4). Nul -> placeholder dessiné.
		void SetRaceViewport(engine::render::race::RacePreviewViewport* vp) { m_raceViewport = vp; }
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }
		void SetVisible(bool v) { m_visible = v; }
		void ToggleVisible() { m_visible = !m_visible; }
		bool IsVisible() const { return m_visible; }
		void SetActiveTab(Tab t) { m_activeTab = t; }
		Tab ActiveTab() const { return m_activeTab; }
		/// Ouvre la fenêtre ET force l'onglet demandé (sélection programmatique au
		/// prochain rendu). Utilisé par les slash commands /skills //grimoire //arbre
		/// et l'item de menu pause pour router vers la fenêtre unifiée.
		void OpenAtTab(Tab t) { m_visible = true; m_pendingTab = t; m_hasPendingTab = true; }
		/// Remet l'aperçu 3D face à la caméra (appelé à l'ouverture F1).
		void ResetPreviewOrientation() { m_previewYaw = 0.0f; }

		/// À appeler entre ImGui::NewFrame() et ImGui::Render(), dans le bloc des
		/// panneaux (single-pass). No-op si non visible.
		void Render(const engine::client::UIModel& model);

	private:
		void RenderPersonnageTab(const engine::client::UIModel& model);

		const engine::core::Config* m_cfg = nullptr;
		const engine::client::UIModelBinding* m_uiBinding = nullptr;
		const engine::client::InventoryUiPresenter* m_inv = nullptr;
		const engine::items::ItemCatalog* m_itemCatalog = nullptr; ///< Chantier 2 SP-A
		PendingEquipAction m_pendingEquip{};                       ///< Chantier 2 SP-A
		engine::client::SkillIconCache* m_icons = nullptr;
		engine::render::SkillBookImGuiRenderer* m_skillBook = nullptr;
		engine::render::GrimoireImGuiRenderer* m_grimoire = nullptr;
		engine::render::ClassSkillTreeImGuiRenderer* m_classTree = nullptr;
		engine::render::ExploitsImGuiRenderer* m_exploits = nullptr; ///< SP2 anniversaires
		engine::render::race::RacePreviewViewport* m_raceViewport = nullptr;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
		bool m_visible = false;
		Tab m_activeTab = Tab::Personnage;
		Tab m_pendingTab = Tab::Personnage;   ///< Onglet à forcer (OpenAtTab).
		bool m_hasPendingTab = false;         ///< Vrai tant que la sélection forcée n'est pas consommée.
		float m_previewYaw = 0.0f;            ///< Angle orbit du perso 3D (drag souris).
	};
}
