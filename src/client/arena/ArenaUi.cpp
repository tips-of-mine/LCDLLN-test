// CMANGOS.21 (Phase 5.21 step 3+4) — Implementation ArenaUiPresenter.

#include "src/client/arena/ArenaUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	ArenaUiPresenter::~ArenaUiPresenter()
	{
		Shutdown();
	}

	bool ArenaUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ArenaUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingTeamId = 0;
		m_pendingSize   = 0;
		LOG_INFO(Core, "[ArenaUiPresenter] Init OK");
		return true;
	}

	void ArenaUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[ArenaUiPresenter] Destroyed");
	}

	void ArenaUiPresenter::ClearQueueState()
	{
		m_state.inQueue          = false;
		m_state.queuedTeamId     = 0;
		m_state.queuedSize       = 0;
		m_state.estimatedWaitSec = 0;
	}

	void ArenaUiPresenter::ClearProposalState()
	{
		m_state.pendingProposalId.reset();
		m_state.pendingOpponentName.clear();
		m_state.pendingOpponentRating = 0;
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void ArenaUiPresenter::RequestTeams()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[ArenaUiPresenter] RequestTeams: no send callback");
			return;
		}
		const auto payload = engine::network::BuildArenaTeamListRequestPayload();
		if (!m_send(engine::network::kOpcodeArenaTeamListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste arena teams).";
			LOG_WARN(Net, "[ArenaUiPresenter] RequestTeams: send failed");
			return;
		}
		LOG_DEBUG(Net, "[ArenaUiPresenter] ArenaTeamListRequest queued");
	}

	void ArenaUiPresenter::Queue(uint32_t teamId, uint8_t size)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[ArenaUiPresenter] Queue: no send callback");
			return;
		}
		// Validation cote client : size 2/3/5, teamId > 0. Le master validera
		// quand meme, mais on evite un round-trip pour des inputs invalides.
		if (size != 2u && size != 3u && size != 5u)
		{
			m_state.lastErrorText = "Taille arene invalide (2/3/5 attendus).";
			LOG_WARN(Net, "[ArenaUiPresenter] Queue: invalid size={}",
				static_cast<unsigned>(size));
			return;
		}
		if (teamId == 0u)
		{
			m_state.lastErrorText = "Equipe invalide.";
			LOG_WARN(Net, "[ArenaUiPresenter] Queue: invalid teamId");
			return;
		}

		const auto payload = engine::network::BuildArenaQueueRequestPayload(teamId, size);
		m_pendingTeamId = teamId;
		m_pendingSize   = size;
		if (!m_send(engine::network::kOpcodeArenaQueueRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (queue arena).";
			LOG_WARN(Net, "[ArenaUiPresenter] Queue: send failed");
			return;
		}
		LOG_DEBUG(Net, "[ArenaUiPresenter] ArenaQueueRequest queued (teamId={}, size={})",
			teamId, static_cast<unsigned>(size));
	}

	void ArenaUiPresenter::LeaveQueue()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[ArenaUiPresenter] LeaveQueue: no send callback");
			return;
		}
		const auto payload = engine::network::BuildArenaLeaveQueueRequestPayload();
		if (!m_send(engine::network::kOpcodeArenaLeaveQueueRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (leave queue arena).";
			LOG_WARN(Net, "[ArenaUiPresenter] LeaveQueue: send failed");
			return;
		}
		LOG_DEBUG(Net, "[ArenaUiPresenter] ArenaLeaveQueueRequest queued");
	}

	void ArenaUiPresenter::AcceptProposal(uint32_t proposalId, bool accept)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[ArenaUiPresenter] AcceptProposal: no send callback");
			return;
		}
		const auto payload = engine::network::BuildArenaMatchAcceptRequestPayload(proposalId, accept);
		if (!m_send(engine::network::kOpcodeArenaMatchAcceptRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (accept match).";
			LOG_WARN(Net, "[ArenaUiPresenter] AcceptProposal: send failed");
			return;
		}
		LOG_INFO(Net, "[ArenaUiPresenter] AcceptProposal sent (proposalId={}, accept={})",
			proposalId, accept ? 1 : 0);

		// Clear le proposal state immediatement pour fermer le popup,
		// independamment de la reponse master.
		ClearProposalState();
		if (!accept)
		{
			ClearQueueState();
			m_state.lastInfoText = "Match refuse, vous etes hors queue.";
		}
		else
		{
			m_state.lastInfoText = "Match accepte. Combat en cours...";
		}
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void ArenaUiPresenter::OnTeamListResponse(const engine::network::ArenaTeamListResponsePayload& resp)
	{
		using engine::network::ArenaErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<ArenaErrorCode>(resp.error);
			if (err == ArenaErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur arena inconnue.";
			LOG_WARN(Net, "[ArenaUiPresenter] OnTeamListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.teams.clear();
		m_state.teams.reserve(resp.teams.size());
		for (const auto& t : resp.teams)
		{
			ArenaTeamSummary s;
			s.teamId      = t.teamId;
			s.size        = t.size;
			s.name        = t.name;
			s.rating      = t.rating;
			s.weeklyGames = t.weeklyGames;
			s.weeklyWins  = t.weeklyWins;
			m_state.teams.push_back(std::move(s));
		}
		m_state.teamsLoaded = true;
		LOG_INFO(Net, "[ArenaUiPresenter] OnTeamListResponse OK count={}",
			m_state.teams.size());
	}

	void ArenaUiPresenter::OnQueueResponse(const engine::network::ArenaQueueResponsePayload& resp)
	{
		using engine::network::ArenaErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<ArenaErrorCode>(resp.error);
			switch (err)
			{
			case ArenaErrorCode::AlreadyQueued:
				m_state.lastErrorText = "Vous etes deja en queue.";
				break;
			case ArenaErrorCode::TeamNotFound:
				m_state.lastErrorText = "Equipe introuvable.";
				break;
			case ArenaErrorCode::InvalidSize:
				m_state.lastErrorText = "Taille arene invalide.";
				break;
			case ArenaErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur arena inconnue.";
				break;
			}
			LOG_WARN(Net, "[ArenaUiPresenter] OnQueueResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.inQueue          = true;
		m_state.queuedTeamId     = m_pendingTeamId;
		m_state.queuedSize       = m_pendingSize;
		m_state.estimatedWaitSec = resp.estimatedWaitSec;
		m_state.lastInfoText     = "Inscrit en queue arene.";
		LOG_INFO(Net, "[ArenaUiPresenter] OnQueueResponse OK estimated={}s",
			resp.estimatedWaitSec);
	}

	void ArenaUiPresenter::OnLeaveQueueResponse(const engine::network::ArenaLeaveQueueResponsePayload& resp)
	{
		using engine::network::ArenaErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<ArenaErrorCode>(resp.error);
			if (err == ArenaErrorCode::NotInQueue)
				m_state.lastErrorText = "Vous n'etes pas en queue.";
			else if (err == ArenaErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur arena inconnue.";
			LOG_WARN(Net, "[ArenaUiPresenter] OnLeaveQueueResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		ClearQueueState();
		ClearProposalState();
		m_state.lastInfoText = "Vous avez quitte la queue.";
		LOG_INFO(Net, "[ArenaUiPresenter] OnLeaveQueueResponse OK");
	}

	void ArenaUiPresenter::OnMatchProposalNotification(const engine::network::ArenaMatchProposalNotificationPayload& note)
	{
		m_state.pendingProposalId     = note.proposalId;
		m_state.pendingOpponentName   = note.opponentTeamName;
		m_state.pendingOpponentRating = note.opponentRating;
		// Le master a retire le joueur de la queue runtime quand le match est
		// forme. On clear notre etat queue local pour rester en sync.
		ClearQueueState();
		m_state.lastInfoText = "Match trouve !";
		LOG_INFO(Net, "[ArenaUiPresenter] OnMatchProposalNotification proposalId={} opp={} rating={}",
			note.proposalId, note.opponentTeamName, note.opponentRating);
	}

	void ArenaUiPresenter::OnMatchAcceptResponse(const engine::network::ArenaMatchAcceptResponsePayload& resp)
	{
		using engine::network::ArenaErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<ArenaErrorCode>(resp.error);
			switch (err)
			{
			case ArenaErrorCode::ProposalExpired:
				m_state.lastErrorText = "Le match propose a expire.";
				break;
			case ArenaErrorCode::UnknownProposal:
				m_state.lastErrorText = "Match inconnu.";
				break;
			case ArenaErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur arena inconnue.";
				break;
			}
			LOG_WARN(Net, "[ArenaUiPresenter] OnMatchAcceptResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}
		m_state.lastErrorText.clear();
		LOG_INFO(Net, "[ArenaUiPresenter] OnMatchAcceptResponse OK");
	}

	void ArenaUiPresenter::OnMatchResultNotification(const engine::network::ArenaMatchResultNotificationPayload& note)
	{
		m_state.lastMatchWin           = note.win;
		m_state.lastMatchOpponent      = note.opponentName;
		// Calcule delta signe (peut etre negatif).
		m_state.lastMatchRatingDelta   = static_cast<int32_t>(note.newRating)
		                                - static_cast<int32_t>(note.oldRating);

		// Update la team locale dont le rating a change. V1 : on cherche par
		// ownership cote presenter (le master n'envoie pas teamId dans le push,
		// mais on a le queuedTeamId memorise — sauf que ClearQueueState a ete
		// appele sur le proposal). Approche pragmatique : refresh la liste a
		// la prochaine ouverture du panel, pour le moment on note juste le delta.
		ClearProposalState();
		m_state.lastInfoText.clear();
		LOG_INFO(Net, "[ArenaUiPresenter] OnMatchResultNotification win={} old={} new={} opp={}",
			note.win ? 1 : 0, note.oldRating, note.newRating, note.opponentName);
	}
}
