// CMANGOS.31 (Phase 5.31 step 3+4) — Implementation GameEventHandler.

#include "src/masterd/handlers/events/GameEventHandler.h"

#include "src/masterd/handlers/lunar/LunarHandler.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/GameEventPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <climits>
#include <vector>

namespace engine::server
{
	using engine::server::events::EventId;
	using engine::server::events::EventState;
	using engine::server::events::GameEventDef;
	using engine::server::events::GameEventManager;

	namespace
	{
		/// Retourne le system_clock now en ms depuis epoch (resolution ms,
		/// wallclock pour comparer aux timestamps absolus des events).
		uint64_t NowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}

		/// Calcule untilTsMs : timestamp de la prochaine bascule pour un
		/// event (def + nowMs + state actuel).
		///
		/// \param def           definition de l'event (timestamps + recur).
		/// \param nowMs         instant courant (system_clock ms epoch).
		/// \param currentState  state courant calcule par GameEventManager.
		/// \return Si Active : timestamp ms de la fin de l'occurrence courante.
		///         Si Inactive : timestamp ms du prochain debut. 0 si aucune
		///         prochaine bascule (one-shot termine).
		uint64_t ComputeUntilTs(const GameEventDef& def, uint64_t nowMs, EventState currentState)
		{
			if (def.recurMs == 0u)
			{
				// One-shot.
				if (currentState == EventState::Active)
				{
					// Event en cours : se termine a startTsMs + durationMs.
					return def.startTsMs + def.durationMs;
				}
				else
				{
					// Inactive : soit pas commence (renvoie startTsMs), soit
					// termine definitivement (renvoie 0).
					if (nowMs < def.startTsMs) return def.startTsMs;
					return 0u;
				}
			}
			// Recurrent.
			if (nowMs < def.startTsMs)
			{
				// Avant la 1ere occurrence.
				return def.startTsMs;
			}
			const uint64_t offset       = (nowMs - def.startTsMs) % def.recurMs;
			const uint64_t cycleStartMs = nowMs - offset;
			if (currentState == EventState::Active)
			{
				// Active : fin de l'occurrence courante.
				return cycleStartMs + def.durationMs;
			}
			else
			{
				// Inactive : prochain debut = debut du cycle suivant.
				return cycleStartMs + def.recurMs;
			}
		}
	}

	// -------------------------------------------------------------------------
	// SeedV1Events — register the 4 hardcoded events at boot.
	// -------------------------------------------------------------------------

	void GameEventHandler::SeedV1Events()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_seeded)
		{
			LOG_DEBUG(Net, "[GameEventHandler] SeedV1Events already seeded (idempotent skip)");
			return;
		}

		// Constantes V1 : durations + recur en ms.
		constexpr uint64_t kDuration14d = 14ull * 24ull * 3600ull * 1000ull;  // 1209600000
		constexpr uint64_t kDuration21d = 21ull * 24ull * 3600ull * 1000ull;  // 1814400000
		constexpr uint64_t kRecur1y     = 365ull * 24ull * 3600ull * 1000ull; // 31536000000

		// Event 1 : Halloween — 2026-10-15 (20741 jours epoch * 86400 * 1000).
		{
			GameEventDef d;
			d.id         = kEventHalloween;
			d.name       = "Halloween";
			d.startTsMs  = 1791849600000ull;
			d.durationMs = kDuration14d;
			d.recurMs    = kRecur1y;
			m_manager.Register(d);
			m_eventNames[kEventHalloween] = "Halloween";
		}

		// Event 2 : Winter Veil — 2026-12-15 (20802 jours epoch * 86400 * 1000).
		{
			GameEventDef d;
			d.id         = kEventWinterVeil;
			d.name       = "Winter Veil";
			d.startTsMs  = 1797206400000ull;
			d.durationMs = kDuration21d;
			d.recurMs    = kRecur1y;
			m_manager.Register(d);
			m_eventNames[kEventWinterVeil] = "Winter Veil";
		}

		// Event 3 : Lunar Festival — 2026-02-01 (20485 jours epoch * 86400 * 1000).
		{
			GameEventDef d;
			d.id         = kEventLunarFestival;
			d.name       = "Lunar Festival";
			d.startTsMs  = 1769817600000ull;
			d.durationMs = kDuration14d;
			d.recurMs    = kRecur1y;
			m_manager.Register(d);
			m_eventNames[kEventLunarFestival] = "Lunar Festival";
		}

		// Event 4 : Midsummer Fire Festival — 2026-06-21 (20625 jours epoch * 86400 * 1000).
		{
			GameEventDef d;
			d.id         = kEventMidsummerFire;
			d.name       = "Midsummer Fire Festival";
			d.startTsMs  = 1781913600000ull;
			d.durationMs = kDuration14d;
			d.recurMs    = kRecur1y;
			m_manager.Register(d);
			m_eventNames[kEventMidsummerFire] = "Midsummer Fire Festival";
		}

		// Event 5 : "Nuit de la Lune Noire" — Phase 5 Lunar gate event.
		// Fil rouge thematique LCDLLN. Toujours actif time-wise
		// (startTs=0, durationMs ~= forever, pas de recurrence) ; gate
		// par les phases lunaires 0 (NewMoon), 14 (EarthshineEarly),
		// 15 (EarthshineLate) via kLunarPhaseNoireMask = 0xC001.
		// Visible dans la liste seulement quand la lune est noire
		// (~21% du temps reel sur le cycle 14j).
		{
			GameEventDef d;
			d.id                       = kEventLuneNoire;
			d.name                     = "Nuit de la Lune Noire";
			d.startTsMs                = 0u;
			d.durationMs               = ULLONG_MAX / 2u;
			d.recurMs                  = 0u;
			d.requiresLunarPhaseMask   = engine::server::events::kLunarPhaseNoireMask;
			m_manager.Register(d);
			m_eventNames[kEventLuneNoire] = "Nuit de la Lune Noire";
		}

		m_seeded = true;
		LOG_INFO(Net, "[GameEventHandler] V1 events seeded : Halloween, Winter Veil, Lunar Festival, Midsummer Fire Festival, Nuit de la Lune Noire");
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void GameEventHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[GameEventHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account.
		uint64_t accountId = 0;
		bool sessionOk = false;
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (connSessionId && *connSessionId != 0u
			&& sessionIdHeader != 0u && *connSessionId == sessionIdHeader)
		{
			auto acc = m_sessionMgr->GetAccountId(*connSessionId);
			if (acc && *acc != 0u)
			{
				accountId = *acc;
				sessionOk = true;
			}
		}

		if (!sessionOk)
		{
			std::vector<uint8_t> pkt;
			const uint8_t kUnauth = static_cast<uint8_t>(GameEventErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeGameEventListRequest:
				pkt = BuildGameEventListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeGameEventSubscribeRequest:
				pkt = BuildGameEventSubscribeResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeGameEventUnsubscribeRequest:
				pkt = BuildGameEventUnsubscribeResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			default:
				return;
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeGameEventListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGameEventSubscribeRequest:
			HandleSubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGameEventUnsubscribeRequest:
			HandleUnsubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// HandleList
	// -------------------------------------------------------------------------

	void GameEventHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		const uint64_t nowMs = NowMs();
		// Phase lunaire courante : si LunarHandler est branche on l'utilise
		// pour filtrer (CurrentPhase prend son propre mutex). Si null,
		// 0 par defaut (et le filtre sera kLunarPhaseAny -> bypass).
		const uint8_t currentLunarPhase = m_lunarHandler
			? m_lunarHandler->CurrentPhase() : static_cast<uint8_t>(0u);
		std::vector<GameEventSummary> events;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto& evs = m_manager.Events();
			events.reserve(evs.size());
			for (const auto& [eid, def] : evs)
			{
				const EventState st = m_lunarHandler
					? m_manager.GetStateFiltered(eid, nowMs, currentLunarPhase)
					: m_manager.GetState(eid, nowMs);
				GameEventSummary summary;
				summary.eventId    = eid;
				summary.name       = def.name;
				summary.state      = static_cast<uint8_t>(st);
				summary.startTsMs  = def.startTsMs;
				summary.durationMs = def.durationMs;
				summary.recurMs    = def.recurMs;
				events.push_back(std::move(summary));
			}
		}

		LOG_INFO(Net, "[GameEventHandler] List account={} count={} lunarPhase={}",
			accountId, events.size(), static_cast<unsigned>(currentLunarPhase));

		auto pkt = BuildGameEventListResponsePacket(0u, events, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleSubscribe
	// -------------------------------------------------------------------------

	void GameEventHandler::HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		// Le payload est vide ; ParseGameEventSubscribeRequestPayload accepte
		// tout buffer (y compris nullptr/0). On ne valide pas le contenu V1.
		(void)ParseGameEventSubscribeRequestPayload(payload, payloadSize);

		const uint64_t nowMs = NowMs();
		// Phase lunaire courante (cf HandleList). Lue avant de prendre
		// m_mutex car CurrentPhase prend son propre mutex (eviter
		// l'inter-locking avec m_lunarHandler).
		const uint8_t currentLunarPhase = m_lunarHandler
			? m_lunarHandler->CurrentPhase() : static_cast<uint8_t>(0u);
		bool alreadySubscribed = false;

		// Snapshot a pousser au nouvel abonne : eventId -> (newState, untilTsMs).
		struct PushItem { uint32_t eventId; uint8_t state; uint64_t untilTsMs; };
		std::vector<PushItem> snapshot;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto inserted = m_subscribers.insert(accountId);
			if (!inserted.second)
			{
				alreadySubscribed = true;
			}
			else
			{
				// Premier subscribe pour cet accountId : compare l'etat
				// actuel des events au dernier broadcast pour ne push que
				// sur changement. Met a jour m_lastBroadcastState.
				const auto& evs = m_manager.Events();
				snapshot.reserve(evs.size());
				for (const auto& [eid, def] : evs)
				{
					const EventState curSt = m_lunarHandler
						? m_manager.GetStateFiltered(eid, nowMs, currentLunarPhase)
						: m_manager.GetState(eid, nowMs);
					const uint8_t curStByte = static_cast<uint8_t>(curSt);
					auto it = m_lastBroadcastState.find(eid);
					const bool changed = (it == m_lastBroadcastState.end())
						|| (it->second != curStByte);
					if (changed)
					{
						const uint64_t untilTs = ComputeUntilTs(def, nowMs, curSt);
						snapshot.push_back(PushItem{ eid, curStByte, untilTs });
						m_lastBroadcastState[eid] = curStByte;
					}
				}
			}
		}

		if (alreadySubscribed)
		{
			LOG_INFO(Net, "[GameEventHandler] Subscribe AlreadySubscribed account={}", accountId);
			auto pkt = BuildGameEventSubscribeResponsePacket(
				static_cast<uint8_t>(GameEventErrorCode::AlreadySubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[GameEventHandler] Subscribe OK account={} pushCount={}",
			accountId, snapshot.size());

		// Reponse OK.
		auto pkt = BuildGameEventSubscribeResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// Push StateChangeNotification au nouvel abonne pour chaque event
		// dont l'etat differe du dernier broadcast. V1 : pas de broadcast
		// cross-subscribers (tick periodique a venir).
		for (const auto& item : snapshot)
		{
			PushStateChange(connId, item.eventId, item.state, item.untilTsMs);
		}
	}

	// -------------------------------------------------------------------------
	// HandleUnsubscribe
	// -------------------------------------------------------------------------

	void GameEventHandler::HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		(void)ParseGameEventUnsubscribeRequestPayload(payload, payloadSize);

		bool wasSubscribed = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto erased = m_subscribers.erase(accountId);
			if (erased > 0)
				wasSubscribed = true;
		}

		if (!wasSubscribed)
		{
			LOG_INFO(Net, "[GameEventHandler] Unsubscribe NotSubscribed account={}", accountId);
			auto pkt = BuildGameEventUnsubscribeResponsePacket(
				static_cast<uint8_t>(GameEventErrorCode::NotSubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[GameEventHandler] Unsubscribe OK account={}", accountId);
		auto pkt = BuildGameEventUnsubscribeResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// PushStateChange
	// -------------------------------------------------------------------------

	bool GameEventHandler::PushStateChange(uint32_t connId, uint32_t eventId, uint8_t newState, uint64_t untilTsMs)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[GameEventHandler] PushStateChange dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[GameEventHandler] PushStateChange: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildGameEventStateChangeNotificationPacket(eventId, newState, untilTsMs, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[GameEventHandler] PushStateChange: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[GameEventHandler] PushStateChange connId={} eid={} state={} untilTsMs={}",
			connId, eventId, static_cast<unsigned>(newState), untilTsMs);
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint64_t GameEventHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
