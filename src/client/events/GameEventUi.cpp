// CMANGOS.31 (Phase 5.31 step 3+4) — Implementation GameEventUiPresenter.

#include "src/client/events/GameEventUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <cstdio>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// GameEventStateName — static helper.
	// -------------------------------------------------------------------------

	const char* GameEventStateName(uint8_t state)
	{
		switch (state)
		{
		case 0: return "Inactive";
		case 1: return "Active";
		default: return "?";
		}
	}

	// -------------------------------------------------------------------------
	// FormatRelativeTime — static helper.
	// -------------------------------------------------------------------------

	std::string FormatRelativeTime(int64_t deltaMs)
	{
		if (deltaMs <= 0)
			return std::string("-");

		const int64_t seconds = deltaMs / 1000;
		const int64_t days    = seconds / 86400;
		const int64_t hours   = (seconds % 86400) / 3600;
		const int64_t minutes = (seconds % 3600) / 60;
		const int64_t secs    = seconds % 60;

		char buf[64]{};
		if (days > 0)
		{
			std::snprintf(buf, sizeof(buf), "%lldd %lldh",
				static_cast<long long>(days),
				static_cast<long long>(hours));
		}
		else if (hours > 0)
		{
			std::snprintf(buf, sizeof(buf), "%lldh %lldm",
				static_cast<long long>(hours),
				static_cast<long long>(minutes));
		}
		else if (minutes > 0)
		{
			std::snprintf(buf, sizeof(buf), "%lldm %llds",
				static_cast<long long>(minutes),
				static_cast<long long>(secs));
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "%llds",
				static_cast<long long>(secs));
		}
		return std::string(buf);
	}

	namespace
	{
		/// Retourne le steady_clock now en ms depuis le boot pour
		/// l'horodatage local des toasts (5s d'affichage).
		uint64_t SteadyMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	// -------------------------------------------------------------------------
	// Presenter lifecycle
	// -------------------------------------------------------------------------

	GameEventUiPresenter::~GameEventUiPresenter()
	{
		Shutdown();
	}

	bool GameEventUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[GameEventUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[GameEventUiPresenter] Init OK");
		return true;
	}

	void GameEventUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[GameEventUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void GameEventUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GameEventUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGameEventListRequestPayload();
		if (!m_send(engine::network::kOpcodeGameEventListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste events).";
			LOG_WARN(Net, "[GameEventUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[GameEventUiPresenter] GameEvent ListRequest queued");
	}

	void GameEventUiPresenter::Subscribe()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GameEventUiPresenter] Subscribe: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGameEventSubscribeRequestPayload();
		if (!m_send(engine::network::kOpcodeGameEventSubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (subscribe events).";
			LOG_WARN(Net, "[GameEventUiPresenter] Subscribe: send failed");
			return;
		}
		LOG_DEBUG(Net, "[GameEventUiPresenter] SubscribeRequest queued");
	}

	void GameEventUiPresenter::Unsubscribe()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GameEventUiPresenter] Unsubscribe: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGameEventUnsubscribeRequestPayload();
		if (!m_send(engine::network::kOpcodeGameEventUnsubscribeRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (unsubscribe events).";
			LOG_WARN(Net, "[GameEventUiPresenter] Unsubscribe: send failed");
			return;
		}
		LOG_DEBUG(Net, "[GameEventUiPresenter] UnsubscribeRequest queued");
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void GameEventUiPresenter::OnListResponse(const engine::network::GameEventListResponsePayload& resp)
	{
		using engine::network::GameEventErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GameEventErrorCode>(resp.error);
			if (err == GameEventErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur GameEvent inconnue.";
			LOG_WARN(Net, "[GameEventUiPresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.events.clear();
		m_state.events.reserve(resp.events.size());
		for (const auto& ev : resp.events)
		{
			GameEventSummary local;
			local.eventId    = ev.eventId;
			local.name       = ev.name;
			local.state      = ev.state;
			local.startTsMs  = ev.startTsMs;
			local.durationMs = ev.durationMs;
			local.recurMs    = ev.recurMs;
			local.untilTsMs  = 0u; // sera renseigne par le prochain StateChange.
			m_state.events.push_back(std::move(local));
		}
		m_state.eventsLoaded = true;

		LOG_INFO(Net, "[GameEventUiPresenter] OnListResponse OK count={}",
			m_state.events.size());
	}

	void GameEventUiPresenter::OnSubscribeResponse(const engine::network::GameEventSubscribeResponsePayload& resp)
	{
		using engine::network::GameEventErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GameEventErrorCode>(resp.error);
			switch (err)
			{
			case GameEventErrorCode::AlreadySubscribed:
				m_state.lastErrorText = "Deja abonne aux events.";
				// Reflete l'etat reel cote serveur (si on s'est abonne dans
				// une autre session par exemple).
				m_state.subscribed = true;
				break;
			case GameEventErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur GameEvent inconnue.";
				break;
			}
			LOG_WARN(Net, "[GameEventUiPresenter] OnSubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.subscribed = true;
		m_state.lastInfoText = "Abonne aux push d'events.";
		LOG_INFO(Net, "[GameEventUiPresenter] OnSubscribeResponse OK");
	}

	void GameEventUiPresenter::OnUnsubscribeResponse(const engine::network::GameEventUnsubscribeResponsePayload& resp)
	{
		using engine::network::GameEventErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GameEventErrorCode>(resp.error);
			switch (err)
			{
			case GameEventErrorCode::NotSubscribed:
				m_state.lastErrorText = "Pas abonne aux events.";
				m_state.subscribed = false;
				break;
			case GameEventErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			default:
				m_state.lastErrorText = "Erreur GameEvent inconnue.";
				break;
			}
			LOG_WARN(Net, "[GameEventUiPresenter] OnUnsubscribeResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.subscribed = false;
		m_state.lastInfoText = "Desabonne des push d'events.";
		LOG_INFO(Net, "[GameEventUiPresenter] OnUnsubscribeResponse OK");
	}

	void GameEventUiPresenter::OnStateChangeNotification(const engine::network::GameEventStateChangeNotificationPayload& note)
	{
		// Mise a jour locale du cache events.
		for (auto& ev : m_state.events)
		{
			if (ev.eventId == note.eventId)
			{
				ev.state     = note.newState;
				ev.untilTsMs = note.untilTsMs;
				break;
			}
		}
		// Met a jour le toast (lastChange*).
		m_state.lastChangeEventId  = note.eventId;
		m_state.lastChangeNewState = note.newState;
		m_state.lastChangeTimeMs   = SteadyMs();
		LOG_DEBUG(Net, "[GameEventUiPresenter] OnStateChangeNotification eid={} state={} untilTsMs={}",
			note.eventId, static_cast<unsigned>(note.newState), note.untilTsMs);
	}
}
