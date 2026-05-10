// CMANGOS.42 (Phase 4.42 step 3+4) — Implementation WeatherUiPresenter.

#include "src/client/weather/WeatherUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::client
{
	// -------------------------------------------------------------------------
	// WeatherKindName — static helper.
	// -------------------------------------------------------------------------

	const char* WeatherKindName(uint8_t kind)
	{
		switch (kind)
		{
		case 0: return "Clear";
		case 1: return "Rain";
		case 2: return "Snow";
		case 3: return "Storm";
		case 4: return "Sandstorm";
		case 5: return "Fog";
		default: return "?";
		}
	}

	// -------------------------------------------------------------------------
	// Presenter lifecycle
	// -------------------------------------------------------------------------

	WeatherUiPresenter::~WeatherUiPresenter()
	{
		Shutdown();
	}

	bool WeatherUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[WeatherUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingSubscribeZoneId   = 0;
		m_pendingUnsubscribeZoneId = 0;
		LOG_INFO(Core, "[WeatherUiPresenter] Init OK");
		return true;
	}

	void WeatherUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[WeatherUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void WeatherUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[WeatherUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildWeatherListRequestPayload();
		if (!m_send(engine::network::kOpcodeWeatherListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste meteo).";
			LOG_WARN(Net, "[WeatherUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[WeatherUiPresenter] Weather ListRequest queued");
	}

	void WeatherUiPresenter::Subscribe(uint32_t zoneId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[WeatherUiPresenter] Subscribe: no send callback");
			return;
		}
		if (zoneId == 0u)
		{
			m_state.lastErrorText = "Zone invalide.";
			LOG_WARN(Net, "[WeatherUiPresenter] Subscribe: invalid zoneId=0");
			return;
		}
		const auto payload = engine::network::BuildWeatherSubscribeRequestPayload(zoneId);
		m_pendingSubscribeZoneId = zoneId;
		if (!m_send(engine::network::kOpcodeWeatherSubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (subscribe meteo).";
			LOG_WARN(Net, "[WeatherUiPresenter] Subscribe: send failed zoneId={}", zoneId);
			return;
		}
		LOG_DEBUG(Net, "[WeatherUiPresenter] SubscribeRequest queued (zoneId={})", zoneId);
	}

	void WeatherUiPresenter::Unsubscribe(uint32_t zoneId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[WeatherUiPresenter] Unsubscribe: no send callback");
			return;
		}
		if (zoneId == 0u)
		{
			m_state.lastErrorText = "Zone invalide.";
			LOG_WARN(Net, "[WeatherUiPresenter] Unsubscribe: invalid zoneId=0");
			return;
		}
		const auto payload = engine::network::BuildWeatherUnsubscribeRequestPayload(zoneId);
		m_pendingUnsubscribeZoneId = zoneId;
		if (!m_send(engine::network::kOpcodeWeatherUnsubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (unsubscribe meteo).";
			LOG_WARN(Net, "[WeatherUiPresenter] Unsubscribe: send failed zoneId={}", zoneId);
			return;
		}
		LOG_DEBUG(Net, "[WeatherUiPresenter] UnsubscribeRequest queued (zoneId={})", zoneId);
	}

	void WeatherUiPresenter::SetActiveZone(uint32_t zoneId)
	{
		if (zoneId == 0u)
		{
			ClearActiveZone();
			return;
		}
		m_state.activeZoneId = zoneId;
		// Initialise activeKind / activeIntensity depuis le cache si dispo.
		for (const auto& z : m_state.zones)
		{
			if (z.zoneId == zoneId)
			{
				m_state.activeKind      = z.kind;
				m_state.activeIntensity = z.intensity;
				break;
			}
		}
		LOG_INFO(Core, "[WeatherUiPresenter] SetActiveZone zoneId={} kind={} intensity={:.3f}",
			zoneId, static_cast<unsigned>(m_state.activeKind),
			static_cast<double>(m_state.activeIntensity));
	}

	void WeatherUiPresenter::ClearActiveZone()
	{
		m_state.activeZoneId.reset();
		m_state.activeKind      = 0;
		m_state.activeIntensity = 0.0f;
		LOG_INFO(Core, "[WeatherUiPresenter] ClearActiveZone");
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void WeatherUiPresenter::OnListResponse(const engine::network::WeatherListResponsePayload& resp)
	{
		using engine::network::WeatherErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<WeatherErrorCode>(resp.error);
			if (err == WeatherErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur Weather inconnue.";
			LOG_WARN(Net, "[WeatherUiPresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.zones.clear();
		m_state.zones.reserve(resp.zones.size());
		for (const auto& z : resp.zones)
		{
			WeatherZoneSummary local;
			local.zoneId    = z.zoneId;
			local.name      = z.name;
			local.kind      = z.kind;
			local.intensity = z.intensity;
			m_state.zones.push_back(std::move(local));
		}
		m_state.zonesLoaded = true;

		// Si la zone active est dans le cache, met a jour activeKind/intensity.
		if (m_state.activeZoneId.has_value())
		{
			for (const auto& z : m_state.zones)
			{
				if (z.zoneId == *m_state.activeZoneId)
				{
					m_state.activeKind      = z.kind;
					m_state.activeIntensity = z.intensity;
					break;
				}
			}
		}

		LOG_INFO(Net, "[WeatherUiPresenter] OnListResponse OK count={}",
			m_state.zones.size());
	}

	void WeatherUiPresenter::OnSubscribeResponse(const engine::network::WeatherSubscribeResponsePayload& resp)
	{
		using engine::network::WeatherErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<WeatherErrorCode>(resp.error);
			switch (err)
			{
			case WeatherErrorCode::UnknownZone:
				m_state.lastErrorText = "Zone meteo inconnue.";
				break;
			case WeatherErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur Weather inconnue.";
				break;
			}
			LOG_WARN(Net, "[WeatherUiPresenter] OnSubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		if (m_pendingSubscribeZoneId != 0u)
		{
			const uint32_t zid = m_pendingSubscribeZoneId;
			m_state.subscribedZones.insert(zid);
			m_state.lastInfoText = "Abonne aux push de la zone meteo.";
			// Mise a jour locale du cache pour la zone subscribed avec
			// kind + intensity initiales retournes par le master.
			for (auto& z : m_state.zones)
			{
				if (z.zoneId == zid)
				{
					z.kind      = resp.currentKind;
					z.intensity = resp.currentIntensity;
					break;
				}
			}
			// Si la zone subscribed est l'activeZone, met a jour le HUD.
			if (m_state.activeZoneId.has_value() && *m_state.activeZoneId == zid)
			{
				m_state.activeKind      = resp.currentKind;
				m_state.activeIntensity = resp.currentIntensity;
			}
			LOG_INFO(Net, "[WeatherUiPresenter] OnSubscribeResponse OK zoneId={} kind={} intensity={:.3f}",
				zid, static_cast<unsigned>(resp.currentKind),
				static_cast<double>(resp.currentIntensity));
			m_pendingSubscribeZoneId = 0u;
		}
	}

	void WeatherUiPresenter::OnUnsubscribeResponse(const engine::network::WeatherUnsubscribeResponsePayload& resp)
	{
		using engine::network::WeatherErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<WeatherErrorCode>(resp.error);
			switch (err)
			{
			case WeatherErrorCode::NotSubscribed:
				m_state.lastErrorText = "Pas abonne a cette zone meteo.";
				break;
			case WeatherErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur Weather inconnue.";
				break;
			}
			LOG_WARN(Net, "[WeatherUiPresenter] OnUnsubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		if (m_pendingUnsubscribeZoneId != 0u)
		{
			m_state.subscribedZones.erase(m_pendingUnsubscribeZoneId);
			m_state.lastInfoText = "Desabonne de la zone meteo.";
			LOG_INFO(Net, "[WeatherUiPresenter] OnUnsubscribeResponse OK zoneId={}",
				m_pendingUnsubscribeZoneId);
			m_pendingUnsubscribeZoneId = 0u;
		}
	}

	void WeatherUiPresenter::OnUpdateNotification(const engine::network::WeatherUpdateNotificationPayload& note)
	{
		// Mise a jour locale du cache zones.
		for (auto& z : m_state.zones)
		{
			if (z.zoneId == note.zoneId)
			{
				z.kind      = note.kind;
				z.intensity = note.intensity;
				break;
			}
		}
		// Si zone active, met a jour le HUD aussi.
		if (m_state.activeZoneId.has_value() && *m_state.activeZoneId == note.zoneId)
		{
			m_state.activeKind      = note.kind;
			m_state.activeIntensity = note.intensity;
		}
		LOG_DEBUG(Net, "[WeatherUiPresenter] OnUpdateNotification zid={} kind={} intensity={:.3f}",
			note.zoneId, static_cast<unsigned>(note.kind),
			static_cast<double>(note.intensity));
	}
}
