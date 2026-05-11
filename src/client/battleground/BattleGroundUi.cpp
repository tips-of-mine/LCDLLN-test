// CMANGOS.10 (Phase 5 step 3+4) — Implementation BattleGroundUiPresenter.

#include "src/client/battleground/BattleGroundUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	BattleGroundUiPresenter::~BattleGroundUiPresenter()
	{
		Shutdown();
	}

	bool BattleGroundUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[BattleGroundUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingBgType  = 0;
		m_pendingFaction = 0;
		LOG_INFO(Core, "[BattleGroundUiPresenter] Init OK");
		return true;
	}

	void BattleGroundUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[BattleGroundUiPresenter] Destroyed");
	}

	void BattleGroundUiPresenter::ClearQueueState()
	{
		m_state.inQueue          = false;
		m_state.queuedBgType     = 0;
		m_state.queuedFaction    = 0;
		m_state.estimatedWaitSec = 0;
		m_state.queuePosition    = 0;
	}

	void BattleGroundUiPresenter::ClearActiveMatch()
	{
		m_state.activeMatchId.reset();
		m_state.activeMatchBgType    = 0;
		m_state.activeMatchMap.clear();
		m_state.activeAllianceCount  = 0;
		m_state.activeHordeCount     = 0;
		m_state.allianceScore        = 0;
		m_state.hordeScore           = 0;
		m_state.matchElapsedSec      = 0;
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void BattleGroundUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[BattleGroundUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildBgListRequestPayload();
		if (!m_send(engine::network::kOpcodeBgListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste BG).";
			LOG_WARN(Net, "[BattleGroundUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[BattleGroundUiPresenter] BgListRequest queued");
	}

	void BattleGroundUiPresenter::Queue(uint16_t bgType, uint8_t faction)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[BattleGroundUiPresenter] Queue: no send callback");
			return;
		}
		// Validation cote client : bgType > 0, faction 0/1.
		if (bgType == 0u)
		{
			m_state.lastErrorText = "BG invalide.";
			LOG_WARN(Net, "[BattleGroundUiPresenter] Queue: invalid bgType=0");
			return;
		}
		if (faction != 0u && faction != 1u)
		{
			m_state.lastErrorText = "Faction invalide (Alliance/Horde attendus).";
			LOG_WARN(Net, "[BattleGroundUiPresenter] Queue: invalid faction={}",
				static_cast<unsigned>(faction));
			return;
		}

		const auto payload = engine::network::BuildBgQueueRequestPayload(bgType, faction);
		m_pendingBgType  = bgType;
		m_pendingFaction = faction;
		if (!m_send(engine::network::kOpcodeBgQueueRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (queue BG).";
			LOG_WARN(Net, "[BattleGroundUiPresenter] Queue: send failed");
			return;
		}
		LOG_DEBUG(Net, "[BattleGroundUiPresenter] BgQueueRequest queued (bgType={}, faction={})",
			static_cast<unsigned>(bgType), static_cast<unsigned>(faction));
	}

	void BattleGroundUiPresenter::LeaveQueue()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[BattleGroundUiPresenter] LeaveQueue: no send callback");
			return;
		}
		const auto payload = engine::network::BuildBgLeaveQueueRequestPayload();
		if (!m_send(engine::network::kOpcodeBgLeaveQueueRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (leave queue BG).";
			LOG_WARN(Net, "[BattleGroundUiPresenter] LeaveQueue: send failed");
			return;
		}
		LOG_DEBUG(Net, "[BattleGroundUiPresenter] BgLeaveQueueRequest queued");
	}

	void BattleGroundUiPresenter::LeaveMatch()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[BattleGroundUiPresenter] LeaveMatch: no send callback");
			return;
		}
		const auto payload = engine::network::BuildBgLeaveMatchRequestPayload();
		if (!m_send(engine::network::kOpcodeBgLeaveMatchRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (forfait BG).";
			LOG_WARN(Net, "[BattleGroundUiPresenter] LeaveMatch: send failed");
			return;
		}
		// Pas de Response paire : on attend le push MatchEndNotification du
		// master pour clear l'etat. On affiche un info transitoire en attendant.
		m_state.lastInfoText = "Forfait demande...";
		LOG_INFO(Net, "[BattleGroundUiPresenter] LeaveMatch sent (forfeit)");
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void BattleGroundUiPresenter::OnListResponse(const engine::network::BgListResponsePayload& resp)
	{
		using engine::network::BgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<BgErrorCode>(resp.error);
			if (err == BgErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur BG inconnue.";
			LOG_WARN(Net, "[BattleGroundUiPresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.battlegrounds.clear();
		m_state.battlegrounds.reserve(resp.battlegrounds.size());
		for (const auto& bg : resp.battlegrounds)
		{
			BattleGroundInfo info;
			info.bgType   = bg.bgType;
			info.name     = bg.name;
			info.teamSize = bg.teamSize;
			info.mapName  = bg.mapName;
			m_state.battlegrounds.push_back(std::move(info));
		}
		m_state.listLoaded = true;
		LOG_INFO(Net, "[BattleGroundUiPresenter] OnListResponse OK count={}",
			m_state.battlegrounds.size());
	}

	void BattleGroundUiPresenter::OnQueueResponse(const engine::network::BgQueueResponsePayload& resp)
	{
		using engine::network::BgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<BgErrorCode>(resp.error);
			switch (err)
			{
			case BgErrorCode::AlreadyQueued:
				m_state.lastErrorText = "Vous etes deja en queue ou en match.";
				break;
			case BgErrorCode::UnknownBg:
				m_state.lastErrorText = "BG inconnu.";
				break;
			case BgErrorCode::InvalidFaction:
				m_state.lastErrorText = "Faction invalide.";
				break;
			case BgErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur BG inconnue.";
				break;
			}
			LOG_WARN(Net, "[BattleGroundUiPresenter] OnQueueResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.inQueue          = true;
		m_state.queuedBgType     = m_pendingBgType;
		m_state.queuedFaction    = m_pendingFaction;
		m_state.estimatedWaitSec = resp.estimatedWaitSec;
		m_state.queuePosition    = resp.queuePosition;
		m_state.lastInfoText     = "Inscrit en queue BG.";
		LOG_INFO(Net, "[BattleGroundUiPresenter] OnQueueResponse OK estimated={}s pos={}",
			resp.estimatedWaitSec, resp.queuePosition);
	}

	void BattleGroundUiPresenter::OnLeaveQueueResponse(const engine::network::BgLeaveQueueResponsePayload& resp)
	{
		using engine::network::BgErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<BgErrorCode>(resp.error);
			if (err == BgErrorCode::NotInQueue)
				m_state.lastErrorText = "Vous n'etes pas en queue.";
			else if (err == BgErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur BG inconnue.";
			LOG_WARN(Net, "[BattleGroundUiPresenter] OnLeaveQueueResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		ClearQueueState();
		m_state.lastInfoText = "Vous avez quitte la queue BG.";
		LOG_INFO(Net, "[BattleGroundUiPresenter] OnLeaveQueueResponse OK");
	}

	void BattleGroundUiPresenter::OnMatchStartNotification(const engine::network::BgMatchStartNotificationPayload& note)
	{
		// Le master a forme le match runtime. On clear l'etat queue local
		// pour rester en sync et on arme l'etat match actif.
		ClearQueueState();
		m_state.activeMatchId         = note.matchId;
		m_state.activeMatchBgType     = note.bgType;
		m_state.activeMatchMap        = note.mapName;
		m_state.activeAllianceCount   = note.allianceCount;
		m_state.activeHordeCount      = note.hordeCount;
		m_state.allianceScore         = 0u;
		m_state.hordeScore            = 0u;
		m_state.matchElapsedSec       = 0u;
		// Reset le toast result du match precedent.
		m_state.lastMatchWinner.reset();
		m_state.lastInfoText = "Match BG demarre !";
		LOG_INFO(Net, "[BattleGroundUiPresenter] OnMatchStartNotification matchId={} bgType={} a={} h={}",
			note.matchId, static_cast<unsigned>(note.bgType),
			static_cast<unsigned>(note.allianceCount), static_cast<unsigned>(note.hordeCount));
	}

	void BattleGroundUiPresenter::OnScoreUpdateNotification(const engine::network::BgScoreUpdateNotificationPayload& note)
	{
		// Filtre par matchId actif (en cas de race avec un MatchEnd recu juste avant).
		if (m_state.activeMatchId.has_value() && *m_state.activeMatchId != note.matchId)
		{
			LOG_DEBUG(Net, "[BattleGroundUiPresenter] OnScoreUpdate skip stale matchId={} (active={})",
				note.matchId, *m_state.activeMatchId);
			return;
		}
		m_state.allianceScore   = note.allianceScore;
		m_state.hordeScore      = note.hordeScore;
		m_state.matchElapsedSec = note.elapsedSec;
		LOG_DEBUG(Net, "[BattleGroundUiPresenter] OnScoreUpdate matchId={} a={} h={} t={}s",
			note.matchId, note.allianceScore, note.hordeScore, note.elapsedSec);
	}

	void BattleGroundUiPresenter::OnMatchEndNotification(const engine::network::BgMatchEndNotificationPayload& note)
	{
		// Arme le toast result transitoire avec le winner + scores + duration.
		m_state.lastMatchWinner        = note.winnerFaction;
		m_state.lastMatchAllianceScore = note.allianceScore;
		m_state.lastMatchHordeScore    = note.hordeScore;
		m_state.lastMatchDurationSec   = note.durationSec;
		// Clear l'etat match actif : le match est termine, on retourne sur la
		// liste BG.
		ClearActiveMatch();
		m_state.lastInfoText.clear();
		LOG_INFO(Net, "[BattleGroundUiPresenter] OnMatchEndNotification matchId={} winner={} a={} h={} dur={}s",
			note.matchId, static_cast<unsigned>(note.winnerFaction),
			note.allianceScore, note.hordeScore, note.durationSec);
	}
}
