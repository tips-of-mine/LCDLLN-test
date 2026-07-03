#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace engine::client
{
	class QuestUiPresenter;
	class QuestTextCatalog;
	class UIModelBinding;
}
namespace engine::core { class Config; }

namespace engine::render
{
	/// SP2 Task 5 — Rendu ImGui in-game des quêtes : journal (liste + détail),
	/// tracker HUD compact et panneau donneur (offer/turn-in du PNJ ciblé).
	/// Windows uniquement (contexte ImGui porté par WorldEditorImGui, cf.
	/// \ref ChatImGuiRenderer). Affichage seul : toute la logique de sélection
	/// de quête reste dans \ref engine::client::QuestUiPresenter ; ce renderer
	/// ne fait que lire \c QuestUiPresenter::GetState() et \c UIModelBinding::GetModel()
	/// et émettre les commandes ImGui.
	///
	/// Position : panneaux ancrés selon \c QuestUiState.journalPanelBounds /
	/// \c trackerBounds (calculés par le presenter). Le panneau donneur est
	/// une fenêtre centrale, visible uniquement quand \c UIModel.giverList
	/// contient des entrées (réponse QuestGiverList reçue après un Talk PNJ).
	class QuestImGuiRenderer final
	{
	public:
		/// Callback émis quand le joueur clique Accepter/Terminer dans le
		/// panneau donneur. \p questId = id texte de la quête (clé catalogue,
		/// PAS l'id numérique interne) ; \p role = 0 (offer -> accept) ou
		/// 1 (turnin -> complete), miroir de \ref engine::client::UIQuestGiverEntry::role.
		/// Câblé par Engine vers \c GameplayUdpClient::SendQuestAcceptRequest /
		/// \c SendQuestTurnInRequest (le renderer n'a pas accès au client UDP).
		using GiverActionCallback = std::function<void(const std::string& questId, uint8_t role)>;

		/// Bind les pointeurs (non possédés). À appeler une fois après init ImGui.
		/// \p presenter fournit journal/tracker (\c QuestUiState). \p textCatalog
		/// résout titres/descriptions/libellés d'étape (jamais synthétisés ici).
		/// \p uiModelBinding fournit \c UIModel.giverList pour le panneau donneur.
		void BindQuestUi(engine::client::QuestUiPresenter* presenter,
			const engine::client::QuestTextCatalog* textCatalog,
			const engine::client::UIModelBinding* uiModelBinding,
			const engine::core::Config* cfg);

		/// Câble le callback d'action du panneau donneur (Accepter/Terminer).
		void SetGiverActionCallback(GiverActionCallback callback) { m_giverAction = std::move(callback); }

		/// Émet les commandes ImGui pour la frame courante. Suppose que
		/// \c m_worldEditorImGui->NewFrame a déjà été appelé.
		/// No-op si le presenter n'est pas bindé.
		///
		/// \p inWorldShard : si false (post-auth mais pas encore in-world), les
		/// panneaux ne sont pas dessinés (pas de quêtes hors monde).
		void Render(float viewportW, float viewportH, bool inWorldShard = true);

	private:
		/// Dessine le panneau journal (liste Active/ReadyToTurnIn + détail).
		void RenderJournal();

		/// Dessine l'encart tracker HUD compact (étapes actives).
		void RenderTracker();

		/// Dessine le panneau donneur (boutons Accepter/Terminer), si
		/// \c UIModel.giverList contient des entrées.
		void RenderGiverPanel();

		/// SP3 Task 3 — Dessine le radar minimap schématique (fond + croix
		/// centrale + marqueurs POI teintés par \c MinimapPoiView::stepType +
		/// marqueur joueur), via \c ImGui::GetForegroundDrawList(). Overlay
		/// non interactif. No-op si \c client.quest.minimap.enabled=false
		/// (config) ou si \c QuestUiState.layoutValid est faux.
		void RenderMinimap();

		engine::client::QuestUiPresenter*        m_presenter = nullptr;
		const engine::client::QuestTextCatalog*  m_textCatalog = nullptr;
		const engine::client::UIModelBinding*    m_uiModelBinding = nullptr;
		const engine::core::Config*              m_cfg = nullptr;
		GiverActionCallback m_giverAction;
	};
}
