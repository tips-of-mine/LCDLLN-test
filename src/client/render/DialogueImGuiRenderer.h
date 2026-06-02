#pragma once

#include <cstdint>

namespace engine::client { class DialoguePresenter; }

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
	};

} // namespace engine::render
