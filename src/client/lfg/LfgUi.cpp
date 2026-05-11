// CMANGOS.33 (Phase 5.33 step 3+4) — Implementation LfgUiPresenter.

#include "src/client/lfg/LfgUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	LfgUiPresenter::~LfgUiPresenter()
	{
		Shutdown();
	}

	bool LfgUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[LfgUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingRole = 0;
		m_pendingDungeonId = 0;
		LOG_INFO(Core, "[LfgUiPresenter] Init OK");
		return true;
	}

	void LfgUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		// Ne pas reset m_send : il est cable une fois au boot et on garde la
		// reference (Engine::Shutdown sera responsable du teardown ordonne).
		LOG_INFO(Core, "[LfgUiPresenter] Destroyed");
	}

	void LfgUiPresenter::ClearQueueState()
	{
		m_state.inQueue           = false;
		m_state.myRole            = 0;
		m_state.myDungeonId       = 0;
		m_state.elapsedSec        = 0;
		m_state.estimatedWaitSec  = 0;
	}

	void LfgUiPresenter::ClearProposalState()
	{
		m_state.hasProposal       = false;
		m_state.proposalId        = 0;
		m_state.proposalDungeonId = 0;
		m_state.proposalMembers.clear();
	}

	// -------------------------------------------------------------------------

	void LfgUiPresenter::RequestQueue(uint8_t role, uint32_t dungeonId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LfgUiPresenter] RequestQueue: no send callback");
			return;
		}
		// Validation cote client : role 0..2, dungeonId > 0. Le master
		// validera quand meme, mais on evite un round-trip pour des inputs
		// evidemment invalides.
		if (role > 2u)
		{
			m_state.lastErrorText = "Role invalide.";
			LOG_WARN(Net, "[LfgUiPresenter] RequestQueue: invalid role={}", static_cast<unsigned>(role));
			return;
		}
		if (dungeonId == 0u)
		{
			m_state.lastErrorText = "Donjon non selectionne.";
			LOG_WARN(Net, "[LfgUiPresenter] RequestQueue: invalid dungeonId");
			return;
		}

		const auto payload = engine::network::BuildLfgQueueRequestPayload(role, dungeonId);
		// Memorise le choix en attente de la reponse Ok.
		m_pendingRole      = role;
		m_pendingDungeonId = dungeonId;
		if (!m_send(engine::network::kOpcodeLfgQueueRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (queue LFG).";
			LOG_WARN(Net, "[LfgUiPresenter] RequestQueue: send failed");
			return;
		}
		LOG_DEBUG(Net, "[LfgUiPresenter] LfgQueueRequest queued (role={}, dungeon={})",
			static_cast<unsigned>(role), dungeonId);
	}

	void LfgUiPresenter::RequestLeave()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LfgUiPresenter] RequestLeave: no send callback");
			return;
		}
		const auto payload = engine::network::BuildLfgLeaveRequestPayload();
		if (!m_send(engine::network::kOpcodeLfgLeaveRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (leave LFG).";
			LOG_WARN(Net, "[LfgUiPresenter] RequestLeave: send failed");
			return;
		}
		LOG_DEBUG(Net, "[LfgUiPresenter] LfgLeaveRequest queued");
	}

	void LfgUiPresenter::RequestStatus()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LfgUiPresenter] RequestStatus: no send callback");
			return;
		}
		const auto payload = engine::network::BuildLfgStatusRequestPayload();
		if (!m_send(engine::network::kOpcodeLfgStatusRequest, payload))
		{
			LOG_WARN(Net, "[LfgUiPresenter] RequestStatus: send failed");
			return;
		}
		LOG_DEBUG(Net, "[LfgUiPresenter] LfgStatusRequest queued");
	}

	void LfgUiPresenter::AcceptMatch(uint64_t proposalId, bool accept)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LfgUiPresenter] AcceptMatch: no send callback");
			return;
		}
		const auto payload = engine::network::BuildLfgMatchAcceptRequestPayload(proposalId, accept);
		if (!m_send(engine::network::kOpcodeLfgMatchAcceptRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (accept match).";
			LOG_WARN(Net, "[LfgUiPresenter] AcceptMatch: send failed");
			return;
		}
		LOG_INFO(Net, "[LfgUiPresenter] AcceptMatch sent (proposalId={}, accept={})",
			proposalId, accept ? 1 : 0);

		// V1 : on clear le proposal state immediatement pour fermer le modal,
		// independamment de la reponse master (qui sera juste un LfgQueueResponse Ok).
		// Si accept == false, on clear aussi l'etat queue local par coherence
		// (le master ne re-queue pas le joueur en V1).
		ClearProposalState();
		if (!accept)
		{
			ClearQueueState();
			m_state.lastInfoText = "Match refuse, vous etes hors queue.";
		}
		else
		{
			m_state.lastInfoText = "Match accepte. En attente du donjon...";
		}
	}

	// -------------------------------------------------------------------------

	void LfgUiPresenter::OnQueueResponse(const engine::network::LfgQueueResponsePayload& resp)
	{
		using engine::network::LfgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<LfgErrorCode>(resp.error);
			switch (err)
			{
			case LfgErrorCode::AlreadyQueued:
				m_state.lastErrorText = "Vous etes deja en queue.";
				break;
			case LfgErrorCode::InvalidRole:
				m_state.lastErrorText = "Role invalide.";
				break;
			case LfgErrorCode::InvalidDungeon:
				m_state.lastErrorText = "Donjon invalide.";
				break;
			case LfgErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case LfgErrorCode::MatchExpired:
				m_state.lastErrorText = "Match expire ou inconnu.";
				break;
			default:
				m_state.lastErrorText = "Erreur LFG inconnue.";
				break;
			}
			LOG_WARN(Net, "[LfgUiPresenter] OnQueueResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.inQueue          = true;
		m_state.myRole           = m_pendingRole;
		m_state.myDungeonId      = m_pendingDungeonId;
		m_state.elapsedSec       = 0;
		m_state.estimatedWaitSec = resp.estimatedWaitSec;
		m_state.lastInfoText     = "Inscrit dans la queue.";
		LOG_INFO(Net, "[LfgUiPresenter] OnQueueResponse OK estimated={}s",
			resp.estimatedWaitSec);
	}

	void LfgUiPresenter::OnLeaveResponse(const engine::network::LfgLeaveResponsePayload& resp)
	{
		using engine::network::LfgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<LfgErrorCode>(resp.error);
			if (err == LfgErrorCode::NotInQueue)
				m_state.lastErrorText = "Vous n'etes pas en queue.";
			else if (err == LfgErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur LFG inconnue.";
			LOG_WARN(Net, "[LfgUiPresenter] OnLeaveResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		ClearQueueState();
		m_state.lastInfoText = "Vous avez quitte la queue.";
		LOG_INFO(Net, "[LfgUiPresenter] OnLeaveResponse OK");
	}

	void LfgUiPresenter::OnStatusResponse(const engine::network::LfgStatusResponsePayload& resp)
	{
		using engine::network::LfgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<LfgErrorCode>(resp.error);
			if (err == LfgErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur LFG inconnue.";
			LOG_WARN(Net, "[LfgUiPresenter] OnStatusResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		if (!resp.inQueue)
		{
			ClearQueueState();
			LOG_DEBUG(Net, "[LfgUiPresenter] OnStatusResponse: not in queue");
			return;
		}
		m_state.inQueue     = true;
		m_state.myRole      = resp.role;
		m_state.myDungeonId = resp.dungeonId;
		m_state.elapsedSec  = resp.elapsedSec;
		LOG_DEBUG(Net, "[LfgUiPresenter] OnStatusResponse: inQueue dungeon={} elapsed={}s",
			resp.dungeonId, resp.elapsedSec);
	}

	void LfgUiPresenter::OnMatchProposal(const engine::network::LfgMatchProposalNotificationPayload& note)
	{
		m_state.hasProposal       = true;
		m_state.proposalId        = note.proposalId;
		m_state.proposalDungeonId = note.dungeonId;
		m_state.proposalMembers.clear();
		m_state.proposalMembers.reserve(note.members.size());
		for (const auto& m : note.members)
		{
			m_state.proposalMembers.emplace_back(m.accountId, m.role);
		}
		// Quand un match est forme, le master a retire le joueur de la queue
		// runtime. On clear notre etat queue local pour rester en sync :
		// si l'utilisateur reject le match, V1 ne le re-queue pas (sub-PR
		// future le fera si besoin).
		ClearQueueState();
		m_state.lastInfoText = "Donjon trouve !";
		LOG_INFO(Net, "[LfgUiPresenter] OnMatchProposal proposalId={} dungeon={} members={}",
			note.proposalId, note.dungeonId, note.members.size());
	}
}
