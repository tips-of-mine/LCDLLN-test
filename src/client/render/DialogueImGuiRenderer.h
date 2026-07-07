#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace engine::client
{
	class DialoguePresenter;
	class UIModelBinding;
	class QuestTextCatalog;
}

namespace engine::render
{
	/// Rendu ImGui de la cellule de dialogue PNJ (fenêtre centrale parchemin,
	/// disposition B). Lit l'état du presenter pour afficher les lignes et les
	/// choix ; émet la sélection de choix et la fermeture via le presenter.
	///
	/// Contrainte : doit être appelé en main thread, pendant la passe ImGui
	/// (après NewFrame, avant Render/EndFrame). Windows uniquement (#if _WIN32
	/// dans le .cpp).
	class DialogueImGuiRenderer final
	{
	public:
		/// PR-B — Callback émis quand le joueur clique un bouton Accepter/Rendre
		/// injecté DANS la conversation (depuis UIModel.giverList). Même contrat
		/// que \ref QuestImGuiRenderer::GiverActionCallback : \p questId = id texte
		/// (clé catalogue), \p role = 0 (accepter) / 1 (rendre). Câblé par Engine
		/// vers SendQuestAcceptRequest / SendQuestTurnInRequest.
		using GiverActionCallback = std::function<void(const std::string& questId, uint8_t role)>;

		/// PR-B — Bind les sources de l'acceptation/rendu de quête intégrés au
		/// dialogue (non possédés) : \p uiModelBinding fournit UIModel.giverList
		/// (instantané status-aware du dernier Talk), \p textCatalog résout les
		/// titres. Si non bindé, aucun bouton de quête n'est injecté (comportement
		/// legacy : dialogue de bavardage seul).
		void BindQuestGiver(const engine::client::UIModelBinding* uiModelBinding,
			const engine::client::QuestTextCatalog* textCatalog)
		{
			m_uiModelBinding = uiModelBinding;
			m_textCatalog = textCatalog;
		}

		/// PR-B — Câble le callback Accepter/Rendre (cf. \ref GiverActionCallback).
		void SetGiverActionCallback(GiverActionCallback callback) { m_giverAction = std::move(callback); }

		/// Dessine la fenêtre de dialogue si le presenter est actif.
		/// \param presenter source de vérité (lecture) + cible des actions joueur
		///        (SelectChoice, Close). Doit rester valide pour toute la durée de l'appel.
		/// \param viewportWidth largeur de la surface de rendu en pixels, pour centrer
		///        la fenêtre.
		/// \param viewportHeight hauteur de la surface de rendu en pixels, pour centrer
		///        la fenêtre.
		/// Effet de bord ImGui : soumet des commandes ImGui (Begin/End, BeginChild/EndChild,
		/// Button, TextUnformatted, SetScrollY…). No-op si le presenter n'est pas actif.
		void Render(engine::client::DialoguePresenter& presenter,
		            float viewportWidth, float viewportHeight);

	private:
		const engine::client::UIModelBinding*   m_uiModelBinding = nullptr;
		const engine::client::QuestTextCatalog* m_textCatalog = nullptr;
		GiverActionCallback                     m_giverAction;
	};

} // namespace engine::render
