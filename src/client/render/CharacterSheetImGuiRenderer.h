#pragma once
// R1-B (Task 4) — CharacterSheetImGuiRenderer : panneau ImGui en lecture seule
// affichant les stats derivees du joueur local (feuille de personnage).
//
// Contrairement aux autres panneaux HUD qui lisent l'etat d'un presenter dedie,
// ce panneau lit directement UIModel.playerStats (peuple par UIModel::ApplyPlayerStats
// a l'enter-world, Task 3). Aucun fetch / parse cote renderer : il ne fait que
// dessiner les champs deja resolus dans le modele.

#include <cstdint>

namespace engine::client { struct UIModel; }

namespace engine::render
{
	/// Renderer ImGui du panneau "Personnage" (feuille de stats derivees).
	/// Strictement en lecture : ne modifie pas le modele, n'envoie aucune requete.
	class CharacterSheetImGuiRenderer
	{
	public:
		CharacterSheetImGuiRenderer() = default;

		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Personnage" depuis \p model.playerStats.
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), seulement
		/// si IsEnabled(). Si playerStats.hasSheet est faux, affiche une ligne
		/// grisee "Statistiques indisponibles".
		/// \param model snapshot UI courant (non possede, lecture seule).
		void Render(const engine::client::UIModel& model);

	private:
		bool     m_enabled  = false;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
	};
}
