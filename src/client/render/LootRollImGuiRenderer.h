#pragma once
// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - LootRollImGuiRenderer : panneau ImGui
// pour la fenetre Loot Roll. Top section : bouton "Simulate Loot Roll" (debug
// V1). Pour chaque pendingRoll : bloc avec item name + count + countdown
// "Time left: 12s" + 3 boutons Need / Greed / Pass (grises si myChoice deja
// set).
//
// Le toast 5s sur dernier RollResult est rendu independamment du flag
// IsEnabled() (les push peuvent arriver panneau ferme), avec couleur selon
// choice : Need=violet, Greed=vert, Pass=gris.
//
// Lit l'etat d'un LootRollUiPresenter, dispatch les inputs UI (Choose /
// SimulateRoll) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class LootRollUiPresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau Loot Roll. Pas de logique de fetch / parse :
	/// celle-ci est dans \ref engine::client::LootRollUiPresenter. Le renderer
	/// ne fait que dessiner l'etat courant et propager les inputs UI vers le
	/// presenter.
	class LootRollImGuiRenderer
	{
	public:
		LootRollImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::LootRollUiPresenter* presenter) { m_presenter = presenter; }

		/// Active/desactive le rendu du panel principal.
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau et du toast.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Loot Roll" (si IsEnabled()) et le toast 5s sur
		/// le dernier RollResult recu (rendu independamment du flag
		/// IsEnabled() puisque les push peuvent arriver panneau ferme).
		/// A appeler entre \c ImGui::NewFrame() et \c ImGui::Render(), apres
		/// NewFrame, si le presenter est valide.
		void Render();

	private:
		/// Dessine le panneau principal (bouton Simulate + cards par roll).
		void RenderMainPanel();

		/// Dessine le toast 5s en bas-droite si lastResultTimeMs est recent.
		void RenderToast();

		engine::client::LootRollUiPresenter* m_presenter = nullptr;
		bool                                 m_enabled   = false;
		uint32_t                             m_viewportW = 0;
		uint32_t                             m_viewportH = 0;
	};
}
