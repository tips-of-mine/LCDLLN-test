#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4) — Presenter client de la fenetre Arena.
// Maintient un cache local des arena teams + etat de queue + popup proposal
// + indicateur transitoire du dernier resultat de match (win/loss + delta ELO).
//
// Pas de rendu ImGui : le panneau est drawe par ArenaImGuiRenderer qui lit
// l'etat via GetState() et propage les inputs UI (RequestTeams / Queue /
// LeaveQueue / AcceptProposal) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans LfgUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 121/123/125/126/128/129
// vers les OnXxx du presenter.

#include "src/shared/network/ArenaPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'une arena team exposable au layer UI. Mirror direct de
	/// engine::network::ArenaTeamSummary.
	struct ArenaTeamSummary
	{
		uint32_t    teamId       = 0;
		uint8_t     size         = 0; ///< 2 / 3 / 5.
		std::string name;
		uint32_t    rating       = 0;
		uint32_t    weeklyGames  = 0;
		uint32_t    weeklyWins   = 0;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct ArenaUiState
	{
		std::vector<ArenaTeamSummary> teams;
		bool                          teamsLoaded = false;

		/// True si l'account est inscrit dans une queue arena.
		bool     inQueue           = false;
		uint32_t queuedTeamId      = 0;
		uint8_t  queuedSize        = 0;
		uint32_t estimatedWaitSec  = 0;

		/// Match proposal en cours (push opcode 126).
		std::optional<uint32_t> pendingProposalId;
		std::string             pendingOpponentName;
		uint32_t                pendingOpponentRating = 0;

		/// Resultat du dernier match (push opcode 129). Affiche transitoirement
		/// par le renderer ; lastMatchExpireAt sert au fade-out cote UI.
		std::optional<bool>     lastMatchWin;
		int32_t                 lastMatchRatingDelta = 0;
		std::string             lastMatchOpponent;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Inscrit en queue."). Vide par defaut.
		std::string lastInfoText;
	};

	/// Presenter pour la fenetre Arena cote client. Doit etre Init() avant tout
	/// usage du callback. Thread : main (comme les autres presenters UI).
	class ArenaUiPresenter final
	{
	public:
		ArenaUiPresenter() = default;

		ArenaUiPresenter(const ArenaUiPresenter&)            = delete;
		ArenaUiPresenter& operator=(const ArenaUiPresenter&) = delete;

		~ArenaUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.21 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestTeams / Queue / LeaveQueue /
		/// AcceptProposal.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie ARENA_TEAM_LIST_REQUEST. Reponse via OnTeamListResponse.
		void RequestTeams();

		/// Envoie ARENA_QUEUE_REQUEST avec \p teamId et \p size (2/3/5).
		/// Reponse via OnQueueResponse.
		void Queue(uint32_t teamId, uint8_t size);

		/// Envoie ARENA_LEAVE_QUEUE_REQUEST. Reponse via OnLeaveQueueResponse.
		void LeaveQueue();

		/// Envoie ARENA_MATCH_ACCEPT_REQUEST avec \p proposalId et \p accept.
		/// Reponse via OnMatchAcceptResponse + push MatchResult si accept=true.
		void AcceptProposal(uint32_t proposalId, bool accept);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit ARENA_TEAM_LIST_RESPONSE. Remplace la cache locale.
		void OnTeamListResponse(const engine::network::ArenaTeamListResponsePayload& resp);

		/// Recoit ARENA_QUEUE_RESPONSE. Si OK, marque inQueue = true et stocke
		/// estimatedWaitSec. Sinon, ecrit dans lastErrorText.
		void OnQueueResponse(const engine::network::ArenaQueueResponsePayload& resp);

		/// Recoit ARENA_LEAVE_QUEUE_RESPONSE. Si OK, clear l'etat de queue local.
		void OnLeaveQueueResponse(const engine::network::ArenaLeaveQueueResponsePayload& resp);

		/// Recoit un push ARENA_MATCH_PROPOSAL_NOTIFICATION : arme le popup
		/// proposal avec proposalId + opponent.
		void OnMatchProposalNotification(const engine::network::ArenaMatchProposalNotificationPayload& note);

		/// Recoit ARENA_MATCH_ACCEPT_RESPONSE. Si OK et accept=true, on attend
		/// le push MatchResult. Si erreur, ecrit dans lastErrorText.
		void OnMatchAcceptResponse(const engine::network::ArenaMatchAcceptResponsePayload& resp);

		/// Recoit un push ARENA_MATCH_RESULT_NOTIFICATION : update la team
		/// localement (rating) + arme le toast result (win/loss + delta).
		void OnMatchResultNotification(const engine::network::ArenaMatchResultNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const ArenaUiState& GetState() const { return m_state; }

	private:
		/// Clear l'etat de queue local (appele a OnLeaveQueueResponse Ok ou a
		/// la reception d'un proposal ; ne touche pas le pending proposal).
		void ClearQueueState();

		/// Clear l'etat proposal local (appele apres AcceptProposal).
		void ClearProposalState();

		/// Memorise le teamId et size choisis en attente de la reponse Ok.
		uint32_t m_pendingTeamId = 0;
		uint8_t  m_pendingSize   = 0;

		bool         m_initialized = false;
		ArenaUiState m_state{};
		SendCallback m_send;
	};
}
