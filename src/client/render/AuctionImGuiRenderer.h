#pragma once
// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — AuctionImGuiRenderer :
// panneau ImGui pour l'Hotel des Ventes. Top section : input filter
// (itemTemplateId) + bouton Refresh. Middle section : table des listings
// (Item / Count / Owner / Current Bid / Buyout / Time Left + bouton
// Bid/Buyout par ligne). Bottom section : formulaire Post (item, count,
// startBid copper, buyout copper, duration 12h/24h/48h).
//
// Le toast 5s sur derniere bid + le toast 5s sur dernier AuctionExpired sont
// rendus independamment du flag IsEnabled() (les push peuvent arriver panneau
// ferme).
//
// Lit l'etat d'un AuctionHousePresenter, dispatch les inputs UI (RequestList /
// Post / Bid / Cancel) via accesseurs / methodes.

#include <cstdint>

namespace engine::client { class AuctionHousePresenter; }

namespace engine::render
{
	/// Renderer ImGui du panneau AuctionHouse. Pas de logique de fetch /
	/// parse : celle-ci est dans \ref engine::client::AuctionHousePresenter.
	/// Le renderer ne fait que dessiner l'etat courant et propager les inputs
	/// UI vers le presenter.
	class AuctionImGuiRenderer
	{
	public:
		AuctionImGuiRenderer() = default;

		/// Cable le presenter (pointeur non possede). \pre presenter init avant Render.
		void SetPresenter(engine::client::AuctionHousePresenter* presenter) { m_presenter = presenter; }

		/// Active/desactive le rendu du panel principal.
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const   { return m_enabled; }

		/// Met a jour la viewport pour le placement du panneau.
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// Render le panel "Auction House" (si IsEnabled()) et les toasts 5s
		/// pour la derniere Bid + le dernier AuctionExpired (rendus
		/// independamment du flag IsEnabled() puisque les push peuvent
		/// arriver panneau ferme). A appeler entre \c ImGui::NewFrame() et
		/// \c ImGui::Render(), apres NewFrame, si le presenter est valide.
		void Render();

	private:
		/// Dessine le panneau principal (top : filter + listings + bid ;
		/// bottom : post form).
		void RenderMainPanel();

		/// Dessine le toast 5s en bas-droite si lastBidTimeMs est recent.
		void RenderBidToast();

		/// Dessine le toast 5s (en dessous du bid toast) si
		/// lastExpirationTimeMs est recent.
		void RenderExpiredToast();

		engine::client::AuctionHousePresenter* m_presenter = nullptr;
		bool                                   m_enabled   = false;
		uint32_t                               m_viewportW = 0;
		uint32_t                               m_viewportH = 0;

		// Buffers ImGui pour les inputs (persistants entre frames).
		int      m_inputFilter         = 0;
		int      m_inputPostItem       = 1;
		int      m_inputPostCount      = 1;
		int      m_inputPostStartBid   = 100;
		int      m_inputPostBuyout     = 0;
		int      m_inputPostDurationIdx= 1;  // 0=12h, 1=24h, 2=48h
		uint64_t m_bidTargetAuctionId  = 0u;
		int      m_inputBidAmount      = 0;
		bool     m_bidPopupOpen        = false;
	};
}
