#pragma once
// CMANGOS.32 (Phase 5.32 step 3+4) — Presenter client de la boite a tickets
// support GM. Maintient un cache local des tickets ouverts par ce joueur,
// la composition d'un nouveau ticket, et expose un compose dialog modal.
//
// Pas de rendu ImGui : le panneau est drawe par GmTicketImGuiRenderer qui
// lit l'etat via GetState() et propage les inputs UI via setters/methodes.
//
// Send : fire-and-forget via un callback (cf. m_send dans QuestUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 77/79/81/82 vers
// les OnXxx du presenter.

#include "src/shared/network/GmTicketPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Une entree de la liste « mes tickets » exposable au layer UI.
	/// Mirror direct de GmTicketEntry sur le wire mais avec des champs nommes
	/// pour le renderer (et eventuellement enrichis a futur, e.g. label
	/// localise pour state).
	struct GmTicketEntryView
	{
		uint64_t id           = 0;
		uint64_t createdTsMs  = 0;
		uint64_t resolvedTsMs = 0;
		uint8_t  state        = 0; ///< 0=Open, 1=Assigned, 2=Resolved, 3=Cancelled.
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct GmTicketUiState
	{
		std::vector<GmTicketEntryView>  mine;
		std::string                     composeBody;
		bool                            isComposeOpen   = false;
		bool                            isListLoading   = false;
		std::string                     lastErrorText;  ///< Vide si pas d'erreur transitoire.
		std::optional<uint64_t>         lastResolvedNotificationTicketId; ///< Pour faire clignoter / toast.
		bool                            layoutValid     = false;
	};

	/// Presenter pour la boite a tickets cote client. Doit etre Init() avant
	/// tout usage du callback. Thread : main (comme les autres presenters UI).
	class GmTicketUiPresenter final
	{
	public:
		GmTicketUiPresenter() = default;

		GmTicketUiPresenter(const GmTicketUiPresenter&)            = delete;
		GmTicketUiPresenter& operator=(const GmTicketUiPresenter&) = delete;

		~GmTicketUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.32 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback. Mirror du pattern QuestUi/MailUi.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestMyTickets / OpenTicket / CancelTicket.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie GMTICKET_LIST_MINE_REQUEST. Reponse via OnListMineResponse.
		/// Met aussi m_state.isListLoading = true le temps de la reponse.
		void RequestMyTickets();

		/// Submit le compose en cours : envoie GMTICKET_OPEN_REQUEST avec
		/// \p body, ferme le compose dialog. Si m_send est null ou body vide,
		/// remplit lastErrorText et n'envoie rien.
		void OpenTicket(std::string_view body);

		/// Envoie GMTICKET_CANCEL_REQUEST pour \p ticketId.
		void CancelTicket(uint64_t ticketId);

		// ---------------------------------------------------------------------
		// Compose dialog
		// ---------------------------------------------------------------------

		/// Ouvre le compose dialog (le renderer lira isComposeOpen). Reset le
		/// composeBody si le dialog n'etait pas deja ouvert.
		void OpenCompose();

		/// Ferme le compose dialog (annule la saisie en cours).
		void CloseCompose();

		/// Met a jour le buffer de saisie depuis l'UI (InputTextMultiline).
		void SetComposeBody(std::string_view body);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit GMTICKET_OPEN_RESPONSE. Si OK, refresh la liste (RequestMyTickets).
		void OnOpenResponse(const engine::network::GmTicketOpenResponsePayload& resp);

		/// Recoit GMTICKET_LIST_MINE_RESPONSE. Remplace la cache locale.
		void OnListMineResponse(const engine::network::GmTicketListMineResponsePayload& resp);

		/// Recoit GMTICKET_CANCEL_RESPONSE. Si OK, refresh la liste.
		void OnCancelResponse(const engine::network::GmTicketCancelResponsePayload& resp);

		/// Recoit GMTICKET_RESOLVED_NOTIFICATION push : note l'evenement dans
		/// le state pour faire clignoter / afficher un toast cote renderer, et
		/// refresh la liste pour mettre a jour l'etat affiche.
		void OnResolvedNotification(const engine::network::GmTicketResolvedNotificationPayload& note);

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const GmTicketUiState& GetState() const { return m_state; }

	private:
		/// Met a jour m_state.mine depuis une reponse server.
		void RebuildMineFromResponse(const engine::network::GmTicketListMineResponsePayload& resp);

		bool                      m_initialized = false;
		GmTicketUiState           m_state{};
		SendCallback              m_send;
	};
}
