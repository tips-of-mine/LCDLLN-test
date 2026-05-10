#pragma once
// CMANGOS.33 (Phase 5.33 step 3+4) — Presenter client de la fenetre LFG
// (LookForGroup). Maintient un cache local de l'etat de queue +
// affiche un modal popup quand un match proposal est recu (push opcode 106).
//
// Pas de rendu ImGui : le panneau est drawe par LfgImGuiRenderer qui lit
// l'etat via GetState() et propage les inputs UI (Queue / Leave /
// AcceptMatch / RejectMatch) via les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans QuestUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 101/103/105/106
// vers les OnXxx du presenter.

#include "src/shared/network/LfgPayloads.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct LfgUiState
	{
		/// True si le joueur est inscrit dans la queue cote master.
		bool     inQueue           = false;
		/// Role choisi par le joueur (significatif si inQueue == true).
		uint8_t  myRole            = 0;
		/// Dungeon choisi (significatif si inQueue == true).
		uint32_t myDungeonId       = 0;
		/// Temps ecoule depuis Join (en secondes), mis a jour a chaque Status.
		uint32_t elapsedSec        = 0;
		/// Estimation initiale renvoyee par le master au Queue (V1 : 60s).
		uint32_t estimatedWaitSec  = 0;

		// Match proposal en cours (push opcode 106).
		bool     hasProposal       = false;
		uint64_t proposalId        = 0;
		uint32_t proposalDungeonId = 0;
		/// Liste des membres du groupe propose (accountId + role).
		std::vector<std::pair<uint64_t, uint8_t>> proposalMembers;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Vous etes en queue"). Vide par defaut.
		std::string lastInfoText;
	};

	/// Presenter pour la fenetre LFG cote client. Doit etre Init() avant tout
	/// usage du callback. Thread : main (comme les autres presenters UI).
	class LfgUiPresenter final
	{
	public:
		LfgUiPresenter() = default;

		LfgUiPresenter(const LfgUiPresenter&)            = delete;
		LfgUiPresenter& operator=(const LfgUiPresenter&) = delete;

		~LfgUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.33 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestQueue / RequestLeave / etc.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie LFG_QUEUE_REQUEST avec \p role et \p dungeonId.
		/// Reponse via \ref OnQueueResponse.
		void RequestQueue(uint8_t role, uint32_t dungeonId);

		/// Envoie LFG_LEAVE_REQUEST. Reponse via \ref OnLeaveResponse.
		void RequestLeave();

		/// Envoie LFG_STATUS_REQUEST. Reponse via \ref OnStatusResponse.
		void RequestStatus();

		/// Envoie LFG_MATCH_ACCEPT_REQUEST avec \p proposalId et \p accept.
		/// Pas de OnXxx dedie en V1 : le master renvoie un LFG_QUEUE_RESPONSE
		/// minimal (Ok). Le presenter clear son state proposal localement.
		void AcceptMatch(uint64_t proposalId, bool accept);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit LFG_QUEUE_RESPONSE. Si OK, marque inQueue = true et stocke
		/// estimatedWaitSec. Sinon, ecrit dans lastErrorText.
		void OnQueueResponse(const engine::network::LfgQueueResponsePayload& resp);

		/// Recoit LFG_LEAVE_RESPONSE. Si OK, clear l'etat de queue local.
		void OnLeaveResponse(const engine::network::LfgLeaveResponsePayload& resp);

		/// Recoit LFG_STATUS_RESPONSE. Met a jour inQueue / role / dungeon /
		/// elapsed selon le payload.
		void OnStatusResponse(const engine::network::LfgStatusResponsePayload& resp);

		/// Recoit un push LFG_MATCH_PROPOSAL_NOTIFICATION : arme le modal
		/// proposal avec proposalId + members + dungeonId.
		void OnMatchProposal(const engine::network::LfgMatchProposalNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const LfgUiState& GetState() const { return m_state; }

	private:
		/// Clear l'etat de queue local (appelee a OnLeaveResponse Ok ou
		/// a la formation d'un groupe). Ne touche pas le proposal en cours.
		void ClearQueueState();

		/// Clear l'etat proposal local (appelee apres AcceptMatch).
		void ClearProposalState();

		/// Memorise le role et dungeon choisi en attente de la reponse Ok.
		uint8_t  m_pendingRole      = 0;
		uint32_t m_pendingDungeonId = 0;

		bool        m_initialized = false;
		LfgUiState  m_state{};
		SendCallback m_send;
	};
}
