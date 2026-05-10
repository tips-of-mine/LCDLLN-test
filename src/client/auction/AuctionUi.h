#pragma once
// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Presenter client de la
// fenetre Hotel des Ventes. Maintient un cache local des listings + champ
// filter actif + memoire des derniers evenements (bid OK, bid buyout, push
// AuctionExpired) pour afficher des toasts.
//
// Pas de rendu ImGui : le panneau est drawe par AuctionImGuiRenderer qui
// lit l'etat via GetState() et propage les inputs UI (RequestList / Post /
// Bid / Cancel) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans GuildUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 174/176/178/180/181 vers les OnXxx du presenter.

#include "src/shared/network/AuctionPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'une enchere expose au layer UI. Mirror direct de
	/// engine::network::AuctionListingSummary.
	struct AuctionListingSummary
	{
		uint64_t    auctionId               = 0;
		uint32_t    itemTemplateId          = 0;
		std::string itemName;
		uint32_t    count                   = 0;
		uint64_t    currentBidCopper        = 0;
		uint64_t    buyoutCopper            = 0;
		std::string ownerName;
		uint64_t    secondsUntilExpiration  = 0;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct AuctionState
	{
		std::vector<AuctionListingSummary> listings;
		bool                               listingsLoaded     = false;

		/// Filter actif sur le itemTemplateId (0 = pas de filtre).
		uint32_t                           filterItemTemplateId = 0;

		/// Toast 5s sur derniere bid recue (succes ou buyout).
		std::optional<uint64_t>            lastBidTimeMs;
		bool                               lastBidWasBuyout    = false;
		uint64_t                           lastBidAuctionId    = 0;

		/// Toast 5s sur dernier AuctionExpired reçu (push asynchrone).
		std::optional<uint64_t>            lastExpirationTimeMs;
		uint64_t                           lastExpirationAuctionId = 0;
		bool                               lastExpirationWon       = false;
		uint64_t                           lastExpirationFinalBid  = 0;
		std::string                        lastExpirationWinnerName;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire. Vide par defaut.
		std::string lastInfoText;
	};

	/// Static helper : formate un montant en copper "12g 34s 56c" (1 gold =
	/// 10000 copper, 1 silver = 100 copper). Ne rend que les composantes
	/// non-nulles, sauf si copper == 0 auquel cas retourne "0c".
	std::string FormatCopper(uint64_t copper);

	/// Static helper : formate une duree en secondes "23h 12m" / "5h" /
	/// "12m" / "expired". Si seconds == 0, retourne "expired".
	std::string FormatDuration(uint64_t seconds);

	/// Presenter pour la fenetre AuctionHouse cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main (comme les autres
	/// presenters UI).
	class AuctionHousePresenter final
	{
	public:
		AuctionHousePresenter() = default;

		AuctionHousePresenter(const AuctionHousePresenter&)            = delete;
		AuctionHousePresenter& operator=(const AuctionHousePresenter&) = delete;

		~AuctionHousePresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.09 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / Post / Bid / Cancel.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie AUCTION_LIST_REQUEST. \p filter (0 = pas de filtre) memorise
		/// dans m_state.filterItemTemplateId. Reponse via OnListResponse.
		void RequestList(uint32_t filter = 0u);

		/// Envoie AUCTION_POST_REQUEST. Validation cote serveur.
		/// Reponse via OnPostResponse.
		///
		/// \param itemTemplateId  id de l'item a poster.
		/// \param count           quantite > 0.
		/// \param startBidCopper  bid initial > 0.
		/// \param buyoutCopper    0 = pas de buyout, sinon >= startBidCopper.
		/// \param durationHours   12, 24 ou 48.
		void Post(uint32_t itemTemplateId, uint32_t count,
			uint64_t startBidCopper, uint64_t buyoutCopper, uint8_t durationHours);

		/// Envoie AUCTION_BID_REQUEST. Reponse via OnBidResponse.
		void Bid(uint64_t auctionId, uint64_t bidAmountCopper);

		/// Envoie AUCTION_CANCEL_REQUEST. Reponse via OnCancelResponse.
		void Cancel(uint64_t auctionId);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit AUCTION_LIST_RESPONSE. Remplace le cache local des listings.
		void OnListResponse(const engine::network::AuctionListResponsePayload& resp);

		/// Recoit AUCTION_POST_RESPONSE. Set lastInfoText sur succes, lastErrorText
		/// sur erreur. Refresh la liste apres un Post reussi.
		void OnPostResponse(const engine::network::AuctionPostResponsePayload& resp);

		/// Recoit AUCTION_BID_RESPONSE. Met a jour lastBid* pour le toast UI.
		void OnBidResponse(const engine::network::AuctionBidResponsePayload& resp);

		/// Recoit AUCTION_CANCEL_RESPONSE. Set lastInfoText sur succes,
		/// lastErrorText sur erreur. Refresh la liste apres un Cancel reussi.
		void OnCancelResponse(const engine::network::AuctionCancelResponsePayload& resp);

		/// Recoit un push AUCTION_EXPIRED_NOTIFICATION : met a jour lastExpiration*
		/// pour le toast UI.
		void OnExpiredNotification(const engine::network::AuctionExpiredNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const AuctionState& GetState() const { return m_state; }

	private:
		bool             m_initialized = false;
		AuctionState     m_state{};
		SendCallback     m_send;

		/// Memoire de l'auctionId du dernier Bid envoye, pour pouvoir
		/// renseigner lastBidAuctionId au moment de la reception de la
		/// reponse (la response ne contient pas l'auctionId).
		uint64_t         m_pendingBidAuctionId = 0u;
	};
}
