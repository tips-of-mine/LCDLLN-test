// CMANGOS.36 (Phase 5.36 step 3+4) — Implementation OutdoorPvpUiPresenter.

#include "src/client/outdoorpvp/OutdoorPvpUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>

namespace engine::client
{
	OutdoorPvpUiPresenter::~OutdoorPvpUiPresenter()
	{
		Shutdown();
	}

	bool OutdoorPvpUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[OutdoorPvpUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingSubscribeZoneId    = 0;
		m_pendingUnsubscribeZoneId  = 0;
		m_pendingCaptureZoneId      = 0;
		m_pendingCaptureObjectiveId = 0;
		m_pendingCaptureFaction     = 0;
		LOG_INFO(Core, "[OutdoorPvpUiPresenter] Init OK");
		return true;
	}

	void OutdoorPvpUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[OutdoorPvpUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void OutdoorPvpUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildOutdoorPvpZoneListRequestPayload();
		if (!m_send(engine::network::kOpcodeOutdoorPvpZoneListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste zones).";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[OutdoorPvpUiPresenter] OutdoorPvp ZoneListRequest queued");
	}

	void OutdoorPvpUiPresenter::Subscribe(uint32_t zoneId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Subscribe: no send callback");
			return;
		}
		if (zoneId == 0u)
		{
			m_state.lastErrorText = "Zone invalide.";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Subscribe: invalid zoneId=0");
			return;
		}
		const auto payload = engine::network::BuildOutdoorPvpSubscribeRequestPayload(zoneId);
		m_pendingSubscribeZoneId = zoneId;
		if (!m_send(engine::network::kOpcodeOutdoorPvpSubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (subscribe).";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Subscribe: send failed zoneId={}", zoneId);
			return;
		}
		LOG_DEBUG(Net, "[OutdoorPvpUiPresenter] SubscribeRequest queued (zoneId={})", zoneId);
	}

	void OutdoorPvpUiPresenter::Unsubscribe(uint32_t zoneId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Unsubscribe: no send callback");
			return;
		}
		if (zoneId == 0u)
		{
			m_state.lastErrorText = "Zone invalide.";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Unsubscribe: invalid zoneId=0");
			return;
		}
		const auto payload = engine::network::BuildOutdoorPvpUnsubscribeRequestPayload(zoneId);
		m_pendingUnsubscribeZoneId = zoneId;
		if (!m_send(engine::network::kOpcodeOutdoorPvpUnsubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (unsubscribe).";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] Unsubscribe: send failed zoneId={}", zoneId);
			return;
		}
		LOG_DEBUG(Net, "[OutdoorPvpUiPresenter] UnsubscribeRequest queued (zoneId={})", zoneId);
	}

	void OutdoorPvpUiPresenter::StartCapture(uint32_t zoneId, uint32_t objectiveId, uint8_t faction)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] StartCapture: no send callback");
			return;
		}
		if (zoneId == 0u || objectiveId == 0u)
		{
			m_state.lastErrorText = "Zone ou objectif invalide.";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] StartCapture: invalid zid={} oid={}",
				zoneId, objectiveId);
			return;
		}
		if (faction != 0u && faction != 1u)
		{
			m_state.lastErrorText = "Faction invalide (Alliance/Horde attendus).";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] StartCapture: invalid faction={}",
				static_cast<unsigned>(faction));
			return;
		}
		const auto payload = engine::network::BuildOutdoorPvpCaptureStartRequestPayload(
			zoneId, objectiveId, faction);
		m_pendingCaptureZoneId      = zoneId;
		m_pendingCaptureObjectiveId = objectiveId;
		m_pendingCaptureFaction     = faction;
		if (!m_send(engine::network::kOpcodeOutdoorPvpCaptureStartRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (capture).";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] StartCapture: send failed");
			return;
		}
		LOG_DEBUG(Net, "[OutdoorPvpUiPresenter] CaptureStartRequest queued (zid={}, oid={}, fac={})",
			zoneId, objectiveId, static_cast<unsigned>(faction));
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void OutdoorPvpUiPresenter::OnListResponse(const engine::network::OutdoorPvpZoneListResponsePayload& resp)
	{
		using engine::network::OutdoorPvpErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<OutdoorPvpErrorCode>(resp.error);
			if (err == OutdoorPvpErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur OutdoorPvp inconnue.";
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.zones.clear();
		m_state.zones.reserve(resp.zones.size());
		for (const auto& z : resp.zones)
		{
			OutdoorPvpZoneSummary local;
			local.zoneId        = z.zoneId;
			local.name          = z.name;
			local.allianceScore = z.allianceScore;
			local.hordeScore    = z.hordeScore;
			local.objectives.reserve(z.objectives.size());
			for (const auto& obj : z.objectives)
			{
				OutdoorPvpObjectiveSummary lo;
				lo.objectiveId = obj.objectiveId;
				lo.owner       = obj.owner;
				lo.capturePct  = obj.capturePct;
				lo.capturingBy = obj.capturingBy;
				local.objectives.push_back(lo);
			}
			m_state.zones.push_back(std::move(local));
		}
		m_state.zonesLoaded = true;
		LOG_INFO(Net, "[OutdoorPvpUiPresenter] OnListResponse OK count={}",
			m_state.zones.size());
	}

	void OutdoorPvpUiPresenter::OnSubscribeResponse(const engine::network::OutdoorPvpSubscribeResponsePayload& resp)
	{
		using engine::network::OutdoorPvpErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<OutdoorPvpErrorCode>(resp.error);
			switch (err)
			{
			case OutdoorPvpErrorCode::UnknownZone:
				m_state.lastErrorText = "Zone inconnue.";
				break;
			case OutdoorPvpErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur OutdoorPvp inconnue.";
				break;
			}
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] OnSubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		if (m_pendingSubscribeZoneId != 0u)
		{
			m_state.subscribedZones.insert(m_pendingSubscribeZoneId);
			m_state.lastInfoText = "Abonne aux push de la zone.";
			LOG_INFO(Net, "[OutdoorPvpUiPresenter] OnSubscribeResponse OK zoneId={}",
				m_pendingSubscribeZoneId);
			m_pendingSubscribeZoneId = 0u;
		}
	}

	void OutdoorPvpUiPresenter::OnUnsubscribeResponse(const engine::network::OutdoorPvpUnsubscribeResponsePayload& resp)
	{
		using engine::network::OutdoorPvpErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<OutdoorPvpErrorCode>(resp.error);
			switch (err)
			{
			case OutdoorPvpErrorCode::NotSubscribed:
				m_state.lastErrorText = "Pas abonne a cette zone.";
				break;
			case OutdoorPvpErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur OutdoorPvp inconnue.";
				break;
			}
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] OnUnsubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		if (m_pendingUnsubscribeZoneId != 0u)
		{
			m_state.subscribedZones.erase(m_pendingUnsubscribeZoneId);
			m_state.lastInfoText = "Desabonne de la zone.";
			LOG_INFO(Net, "[OutdoorPvpUiPresenter] OnUnsubscribeResponse OK zoneId={}",
				m_pendingUnsubscribeZoneId);
			m_pendingUnsubscribeZoneId = 0u;
		}
	}

	void OutdoorPvpUiPresenter::OnCaptureStartResponse(const engine::network::OutdoorPvpCaptureStartResponsePayload& resp)
	{
		using engine::network::OutdoorPvpErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<OutdoorPvpErrorCode>(resp.error);
			switch (err)
			{
			case OutdoorPvpErrorCode::UnknownObjective:
				m_state.lastErrorText = "Objectif inconnu.";
				break;
			case OutdoorPvpErrorCode::InvalidFaction:
				m_state.lastErrorText = "Faction invalide.";
				break;
			case OutdoorPvpErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur OutdoorPvp inconnue.";
				break;
			}
			LOG_WARN(Net, "[OutdoorPvpUiPresenter] OnCaptureStartResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.lastInfoText = "Capture lancee.";
		LOG_INFO(Net, "[OutdoorPvpUiPresenter] OnCaptureStartResponse OK pending zid={} oid={}",
			m_pendingCaptureZoneId, m_pendingCaptureObjectiveId);
	}

	void OutdoorPvpUiPresenter::OnCaptureProgressNotification(const engine::network::OutdoorPvpCaptureProgressNotificationPayload& note)
	{
		m_state.capturingZoneId      = note.zoneId;
		m_state.capturingObjectiveId = note.objectiveId;
		m_state.capturingPct         = note.capturePct;
		m_state.capturingByFaction   = note.capturingBy;

		// Mise a jour locale du cache zones (visualisation immediate sans
		// re-RequestList).
		for (auto& z : m_state.zones)
		{
			if (z.zoneId != note.zoneId)
				continue;
			for (auto& obj : z.objectives)
			{
				if (obj.objectiveId == note.objectiveId)
				{
					obj.capturePct  = note.capturePct;
					obj.capturingBy = note.capturingBy;
					break;
				}
			}
			break;
		}

		LOG_DEBUG(Net, "[OutdoorPvpUiPresenter] OnCaptureProgress zid={} oid={} pct={} fac={}",
			note.zoneId, note.objectiveId, note.capturePct,
			static_cast<unsigned>(note.capturingBy));
	}

	void OutdoorPvpUiPresenter::OnCaptureCompletedNotification(const engine::network::OutdoorPvpCaptureCompletedNotificationPayload& note)
	{
		// Arme le toast transitoire avec le nouveau owner + scores.
		const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		m_state.lastCaptureCompletedTimeMs = static_cast<uint64_t>(nowMs);
		m_state.lastCaptureZoneId          = note.zoneId;
		m_state.lastCaptureObjectiveId     = note.objectiveId;
		m_state.lastCaptureNewOwner        = note.newOwner;
		m_state.lastCaptureAllianceScore   = note.allianceScore;
		m_state.lastCaptureHordeScore      = note.hordeScore;

		// Clear etat capture en cours.
		m_state.capturingZoneId.reset();
		m_state.capturingObjectiveId.reset();
		m_state.capturingPct        = 0u;
		m_state.capturingByFaction  = 0xFFu;

		// Mise a jour locale : owner + scores de la zone, et reset
		// capturePct / capturingBy de l'objectif termine.
		for (auto& z : m_state.zones)
		{
			if (z.zoneId != note.zoneId)
				continue;
			z.allianceScore = note.allianceScore;
			z.hordeScore    = note.hordeScore;
			for (auto& obj : z.objectives)
			{
				if (obj.objectiveId == note.objectiveId)
				{
					obj.owner       = note.newOwner;
					obj.capturePct  = 0u;
					obj.capturingBy = 0xFFu;
					break;
				}
			}
			break;
		}

		m_pendingCaptureZoneId      = 0;
		m_pendingCaptureObjectiveId = 0;
		m_pendingCaptureFaction     = 0;

		LOG_INFO(Net, "[OutdoorPvpUiPresenter] OnCaptureCompleted zid={} oid={} owner={} a={} h={}",
			note.zoneId, note.objectiveId, static_cast<unsigned>(note.newOwner),
			note.allianceScore, note.hordeScore);
	}
}
