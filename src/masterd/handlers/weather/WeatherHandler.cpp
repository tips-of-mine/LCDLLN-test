// CMANGOS.42 (Phase 4.42 step 3+4) — Implementation WeatherHandler.

#include "src/masterd/handlers/weather/WeatherHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/WeatherPayloads.h"

#include <chrono>
#include <vector>

namespace engine::server
{
	using engine::server::weather::WeatherKind;
	using engine::server::weather::WeatherManager;
	using engine::server::weather::ZoneWeatherProfile;
	using engine::server::weather::ZoneWeatherState;

	namespace
	{
		/// Retourne le steady_clock now en ms depuis epoch (resolution OK pour
		/// le Tick de la WeatherManager qui n'a pas besoin d'horloge wallclock).
		uint64_t NowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	// -------------------------------------------------------------------------
	// SeedV1Zones — register the 3 hardcoded zones at boot.
	// -------------------------------------------------------------------------

	void WeatherHandler::SeedV1Zones()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_seeded)
		{
			LOG_DEBUG(Net, "[WeatherHandler] SeedV1Zones already seeded (idempotent skip)");
			return;
		}

		// Init RNG depuis steady_clock (deterministic-ish per-boot, suffisant V1).
		m_rng.seed(static_cast<std::mt19937::result_type>(NowMs()));

		// Zone 1 : Stormwind Plains — climat tempere (Clear/Rain/Storm).
		{
			ZoneWeatherProfile p;
			p.zoneId           = kZoneStormwindPlains;
			p.changeIntervalMs = 60u * 60u * 1000u; // 1h, mais ForceReroll a chaque subscribe.
			p.pClear     = 0.6f;
			p.pRain      = 0.3f;
			p.pSnow      = 0.0f;
			p.pStorm     = 0.1f;
			p.pSandstorm = 0.0f;
			p.pFog       = 0.0f;
			m_manager.RegisterZone(p);
			m_zoneNames[kZoneStormwindPlains] = "Stormwind Plains";
		}

		// Zone 2 : Frozen Tundra — climat polaire (Clear/Snow/Fog).
		{
			ZoneWeatherProfile p;
			p.zoneId           = kZoneFrozenTundra;
			p.changeIntervalMs = 60u * 60u * 1000u;
			p.pClear     = 0.4f;
			p.pRain      = 0.0f;
			p.pSnow      = 0.5f;
			p.pStorm     = 0.0f;
			p.pSandstorm = 0.0f;
			p.pFog       = 0.1f;
			m_manager.RegisterZone(p);
			m_zoneNames[kZoneFrozenTundra] = "Frozen Tundra";
		}

		// Zone 3 : Tanaris Desert — climat aride (Clear/Sandstorm/Fog).
		{
			ZoneWeatherProfile p;
			p.zoneId           = kZoneTanarisDesert;
			p.changeIntervalMs = 60u * 60u * 1000u;
			p.pClear     = 0.5f;
			p.pRain      = 0.0f;
			p.pSnow      = 0.0f;
			p.pStorm     = 0.0f;
			p.pSandstorm = 0.4f;
			p.pFog       = 0.1f;
			m_manager.RegisterZone(p);
			m_zoneNames[kZoneTanarisDesert] = "Tanaris Desert";
		}

		// Premier Tick pour donner des states initiales aux 3 zones.
		m_manager.Tick(NowMs(), m_rng);

		m_seeded = true;
		LOG_INFO(Net, "[WeatherHandler] V1 zones seeded : Stormwind Plains, Frozen Tundra, Tanaris Desert");
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void WeatherHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[WeatherHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(WeatherErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeWeatherListRequest:
				pkt = BuildWeatherListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeWeatherSubscribeRequest:
				pkt = BuildWeatherSubscribeResponsePacket(kUnauth, 0u, 0.0f, requestId, sessionIdHeader);
				break;
			case kOpcodeWeatherUnsubscribeRequest:
				pkt = BuildWeatherUnsubscribeResponsePacket(kUnauth, requestId, sessionIdHeader);
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
		case kOpcodeWeatherListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeWeatherSubscribeRequest:
			HandleSubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeWeatherUnsubscribeRequest:
			HandleUnsubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// HandleList
	// -------------------------------------------------------------------------

	void WeatherHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<WeatherZoneSummary> zones;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const uint32_t zoneIds[] = {
				kZoneStormwindPlains, kZoneFrozenTundra, kZoneTanarisDesert
			};
			for (uint32_t zid : zoneIds)
			{
				const ZoneWeatherState st = m_manager.GetState(zid);
				WeatherZoneSummary summary;
				summary.zoneId    = zid;
				auto nameIt = m_zoneNames.find(zid);
				if (nameIt != m_zoneNames.end())
					summary.name = nameIt->second;
				summary.kind      = static_cast<uint8_t>(st.kind);
				summary.intensity = st.intensity;
				zones.push_back(std::move(summary));
			}
		}

		LOG_INFO(Net, "[WeatherHandler] List account={} count={}",
			accountId, zones.size());

		auto pkt = BuildWeatherListResponsePacket(0u, zones, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleSubscribe
	// -------------------------------------------------------------------------

	void WeatherHandler::HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseWeatherSubscribeRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[WeatherHandler] Subscribe parse failed account={}", accountId);
			auto pkt = BuildWeatherSubscribeResponsePacket(
				static_cast<uint8_t>(WeatherErrorCode::UnknownZone), 0u, 0.0f,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t zoneId = parsed->zoneId;
		bool zoneKnown = false;
		ZoneWeatherState beforeState{};
		ZoneWeatherState afterState{};
		std::vector<uint64_t> subscribersToBroadcast;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			zoneKnown = (m_zoneNames.find(zoneId) != m_zoneNames.end());
			if (zoneKnown)
			{
				m_subscriptions[accountId].insert(zoneId);
				m_zoneSubscribers[zoneId].insert(accountId);

				// Snapshot avant Tick pour detecter le changement.
				beforeState = m_manager.GetState(zoneId);

				// V1 : force reroll de cette zone et tick. Toutes les autres
				// zones avec nextChangeTsMs encore future seront ignorees.
				m_manager.ForceReroll(zoneId);
				m_manager.Tick(NowMs(), m_rng);

				afterState = m_manager.GetState(zoneId);

				// Si etat change, prepare la liste des subscribers pour
				// broadcast. On copie sous mutex pour pouvoir relacher avant
				// d'envoyer (Send peut prendre un mutex aussi, eviter deadlock).
				const bool kindChanged      = (afterState.kind != beforeState.kind);
				const bool intensityChanged = (afterState.intensity != beforeState.intensity);
				if (kindChanged || intensityChanged)
				{
					auto it = m_zoneSubscribers.find(zoneId);
					if (it != m_zoneSubscribers.end())
					{
						subscribersToBroadcast.reserve(it->second.size());
						for (uint64_t accId : it->second)
							subscribersToBroadcast.push_back(accId);
					}
				}
			}
		}

		if (!zoneKnown)
		{
			LOG_INFO(Net, "[WeatherHandler] Subscribe UnknownZone account={} zoneId={}",
				accountId, zoneId);
			auto pkt = BuildWeatherSubscribeResponsePacket(
				static_cast<uint8_t>(WeatherErrorCode::UnknownZone), 0u, 0.0f,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[WeatherHandler] Subscribe OK account={} zoneId={} kind={} intensity={:.3f}",
			accountId, zoneId, static_cast<unsigned>(afterState.kind),
			static_cast<double>(afterState.intensity));

		// Reponse Ok avec le state courant (apres potentiel reroll).
		auto pkt = BuildWeatherSubscribeResponsePacket(0u,
			static_cast<uint8_t>(afterState.kind), afterState.intensity,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// Broadcast WeatherUpdateNotification a tous les subscribers de la
		// zone si l'etat a change suite a notre reroll. Le subscriber qui
		// vient de s'abonner verra le push en plus de la response (idempotent).
		for (uint64_t accId : subscribersToBroadcast)
		{
			const uint32_t targetConn = FindConnIdForAccount(accId);
			if (targetConn == 0u) continue;
			PushWeatherUpdate(targetConn, zoneId,
				static_cast<uint8_t>(afterState.kind), afterState.intensity);
		}
	}

	// -------------------------------------------------------------------------
	// HandleUnsubscribe
	// -------------------------------------------------------------------------

	void WeatherHandler::HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseWeatherUnsubscribeRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[WeatherHandler] Unsubscribe parse failed account={}", accountId);
			auto pkt = BuildWeatherUnsubscribeResponsePacket(
				static_cast<uint8_t>(WeatherErrorCode::NotSubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t zoneId = parsed->zoneId;
		bool wasSubscribed = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_subscriptions.find(accountId);
			if (it != m_subscriptions.end())
			{
				auto erased = it->second.erase(zoneId);
				if (erased > 0)
					wasSubscribed = true;
				if (it->second.empty())
					m_subscriptions.erase(it);
			}
			auto rit = m_zoneSubscribers.find(zoneId);
			if (rit != m_zoneSubscribers.end())
			{
				rit->second.erase(accountId);
				if (rit->second.empty())
					m_zoneSubscribers.erase(rit);
			}
		}

		if (!wasSubscribed)
		{
			LOG_INFO(Net, "[WeatherHandler] Unsubscribe NotSubscribed account={} zoneId={}",
				accountId, zoneId);
			auto pkt = BuildWeatherUnsubscribeResponsePacket(
				static_cast<uint8_t>(WeatherErrorCode::NotSubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[WeatherHandler] Unsubscribe OK account={} zoneId={}",
			accountId, zoneId);
		auto pkt = BuildWeatherUnsubscribeResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// PushWeatherUpdate
	// -------------------------------------------------------------------------

	bool WeatherHandler::PushWeatherUpdate(uint32_t connId, uint32_t zoneId, uint8_t kind, float intensity)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[WeatherHandler] PushWeatherUpdate dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[WeatherHandler] PushWeatherUpdate: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildWeatherUpdateNotificationPacket(zoneId, kind, intensity, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[WeatherHandler] PushWeatherUpdate: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[WeatherHandler] PushWeatherUpdate connId={} zid={} kind={} intensity={:.3f}",
			connId, zoneId, static_cast<unsigned>(kind), static_cast<double>(intensity));
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint64_t WeatherHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}

	uint32_t WeatherHandler::FindConnIdForAccount(uint64_t accountId) const
	{
		if (!m_connMap || !m_sessionMgr) return 0u;
		const auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessionId] : snapshot)
		{
			auto acc = m_sessionMgr->GetAccountId(sessionId);
			if (acc && *acc == accountId)
				return connId;
		}
		return 0u;
	}
}
