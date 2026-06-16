// CMANGOS.36 (Phase 5.36 step 3+4) — Implementation OutdoorPvpHandler.

#include "src/masterd/handlers/outdoorpvp/OutdoorPvpHandler.h"

#include "src/masterd/outdoorpvp/MysqlOutdoorPvpStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/OutdoorPvpPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::server
{
	using engine::server::outdoorpvp::Objective;
	using engine::server::outdoorpvp::Zone;

	// -------------------------------------------------------------------------
	// SeedV1Zones — register the 2 hardcoded zones at boot.
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::SeedV1Zones()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_seeded)
		{
			LOG_DEBUG(Net, "[OutdoorPvpHandler] SeedV1Zones already seeded (idempotent skip)");
			return;
		}

		// Zone 1 : Peninsule Ardente avec 3 objectifs (id 10, 11, 12).
		{
			Zone z;
			z.id = kZoneHellfire;
			z.objectives.push_back(Objective{10u, 0xFFu, 0u, 0xFFu});
			z.objectives.push_back(Objective{11u, 0xFFu, 0u, 0xFFu});
			z.objectives.push_back(Objective{12u, 0xFFu, 0u, 0xFFu});
			m_manager.RegisterZone(z);
			m_zoneNames[kZoneHellfire] = "Peninsule Ardente";
		}

		// Zone 2 : Terres Maudites de l'Est avec 4 objectifs (id 20, 21, 22, 23).
		{
			Zone z;
			z.id = kZoneEasternPlaguelands;
			z.objectives.push_back(Objective{20u, 0xFFu, 0u, 0xFFu});
			z.objectives.push_back(Objective{21u, 0xFFu, 0u, 0xFFu});
			z.objectives.push_back(Objective{22u, 0xFFu, 0u, 0xFFu});
			z.objectives.push_back(Objective{23u, 0xFFu, 0u, 0xFFu});
			m_manager.RegisterZone(z);
			m_zoneNames[kZoneEasternPlaguelands] = "Terres Maudites de l'Est";
		}

		m_seeded = true;
		LOG_INFO(Net, "[OutdoorPvpHandler] V1 zones seeded : Peninsule Ardente (3 obj), Terres Maudites de l'Est (4 obj)");

		// Wave 5 : restaure l'etat persiste par-dessus le seed default. On
		// applique les rows DB ; les objectifs non vus en DB conservent
		// leur state hardcoded (neutre, 0%).
		if (m_store && m_store->IsAvailable())
		{
			const auto stateRows = m_store->LoadStates();
			for (const auto& r : stateRows)
			{
				engine::server::outdoorpvp::Objective patch;
				patch.id          = r.objectiveId;
				patch.owner       = r.owner;
				patch.capturePct  = r.capturePct;
				patch.capturingBy = r.capturingBy;
				// Patch direct via BeginCapture est inadapte (reset capturePct).
				// On reconstruit en allant chercher la zone dans le manager.
				auto* zone = const_cast<engine::server::outdoorpvp::Zone*>(
					m_manager.GetZone(r.zoneId));
				if (!zone) continue;
				for (auto& o : zone->objectives)
				{
					if (o.id == r.objectiveId)
					{
						o.owner       = r.owner;
						o.capturePct  = r.capturePct;
						o.capturingBy = r.capturingBy;
						break;
					}
				}
			}
			const auto scoreRows = m_store->LoadScores();
			for (const auto& r : scoreRows)
			{
				auto* zone = const_cast<engine::server::outdoorpvp::Zone*>(
					m_manager.GetZone(r.zoneId));
				if (!zone) continue;
				zone->score[r.faction] = r.score;
			}
			LOG_INFO(Net, "[OutdoorPvpHandler] Restored {} objective states + {} scores from DB",
				stateRows.size(), scoreRows.size());
		}
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(OutdoorPvpErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeOutdoorPvpZoneListRequest:
				pkt = BuildOutdoorPvpZoneListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeOutdoorPvpSubscribeRequest:
				pkt = BuildOutdoorPvpSubscribeResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeOutdoorPvpUnsubscribeRequest:
				pkt = BuildOutdoorPvpUnsubscribeResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeOutdoorPvpCaptureStartRequest:
				pkt = BuildOutdoorPvpCaptureStartResponsePacket(kUnauth, requestId, sessionIdHeader);
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
		case kOpcodeOutdoorPvpZoneListRequest:
			HandleZoneList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeOutdoorPvpSubscribeRequest:
			HandleSubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeOutdoorPvpUnsubscribeRequest:
			HandleUnsubscribe(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeOutdoorPvpCaptureStartRequest:
			HandleCaptureStart(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// HandleZoneList
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::HandleZoneList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<OutdoorPvpZoneSummary> zones;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const uint32_t zoneIds[] = {kZoneHellfire, kZoneEasternPlaguelands};
			for (uint32_t zid : zoneIds)
			{
				const Zone* zone = m_manager.GetZone(zid);
				if (zone == nullptr)
					continue;
				OutdoorPvpZoneSummary summary;
				summary.zoneId        = zid;
				auto nameIt = m_zoneNames.find(zid);
				if (nameIt != m_zoneNames.end())
					summary.name = nameIt->second;
				summary.allianceScore = m_manager.Score(zid, 0u);
				summary.hordeScore    = m_manager.Score(zid, 1u);
				summary.objectives.reserve(zone->objectives.size());
				for (const auto& obj : zone->objectives)
				{
					OutdoorPvpObjectiveSummary os;
					os.objectiveId = obj.id;
					os.owner       = obj.owner;
					os.capturePct  = obj.capturePct;
					os.capturingBy = obj.capturingBy;
					summary.objectives.push_back(os);
				}
				zones.push_back(std::move(summary));
			}
		}

		LOG_INFO(Net, "[OutdoorPvpHandler] ZoneList account={} count={}",
			accountId, zones.size());

		auto pkt = BuildOutdoorPvpZoneListResponsePacket(0u, zones, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleSubscribe
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::HandleSubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseOutdoorPvpSubscribeRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] Subscribe parse failed account={}", accountId);
			auto pkt = BuildOutdoorPvpSubscribeResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownZone),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		bool zoneKnown = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			zoneKnown = m_manager.HasZone(parsed->zoneId);
			if (zoneKnown)
				m_subscriptions[accountId].insert(parsed->zoneId);
		}

		if (!zoneKnown)
		{
			LOG_INFO(Net, "[OutdoorPvpHandler] Subscribe UnknownZone account={} zoneId={}",
				accountId, parsed->zoneId);
			auto pkt = BuildOutdoorPvpSubscribeResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownZone),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[OutdoorPvpHandler] Subscribe OK account={} zoneId={}",
			accountId, parsed->zoneId);
		auto pkt = BuildOutdoorPvpSubscribeResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleUnsubscribe
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::HandleUnsubscribe(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseOutdoorPvpUnsubscribeRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] Unsubscribe parse failed account={}", accountId);
			auto pkt = BuildOutdoorPvpUnsubscribeResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::NotSubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		bool wasSubscribed = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_subscriptions.find(accountId);
			if (it != m_subscriptions.end())
			{
				auto erased = it->second.erase(parsed->zoneId);
				if (erased > 0)
					wasSubscribed = true;
				if (it->second.empty())
					m_subscriptions.erase(it);
			}
		}

		if (!wasSubscribed)
		{
			LOG_INFO(Net, "[OutdoorPvpHandler] Unsubscribe NotSubscribed account={} zoneId={}",
				accountId, parsed->zoneId);
			auto pkt = BuildOutdoorPvpUnsubscribeResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::NotSubscribed),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[OutdoorPvpHandler] Unsubscribe OK account={} zoneId={}",
			accountId, parsed->zoneId);
		auto pkt = BuildOutdoorPvpUnsubscribeResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleCaptureStart — V1 simule la capture instantanee : 4 progress (25/50/75/100)
	// puis 1 completed avec scores updated.
	// -------------------------------------------------------------------------

	void OutdoorPvpHandler::HandleCaptureStart(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseOutdoorPvpCaptureStartRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] CaptureStart parse failed account={}", accountId);
			auto pkt = BuildOutdoorPvpCaptureStartResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownObjective),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation faction.
		if (parsed->faction != 0u && parsed->faction != 1u)
		{
			LOG_INFO(Net, "[OutdoorPvpHandler] CaptureStart InvalidFaction account={} faction={}",
				accountId, static_cast<unsigned>(parsed->faction));
			auto pkt = BuildOutdoorPvpCaptureStartResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::InvalidFaction),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		uint8_t newOwner = 0xFFu;
		uint32_t allianceScore = 0u;
		uint32_t hordeScore = 0u;
		bool started = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			started = m_manager.BeginCapture(parsed->zoneId, parsed->objectiveId, parsed->faction);
		}

		if (!started)
		{
			LOG_INFO(Net, "[OutdoorPvpHandler] CaptureStart UnknownObjective account={} zid={} oid={}",
				accountId, parsed->zoneId, parsed->objectiveId);
			auto pkt = BuildOutdoorPvpCaptureStartResponsePacket(
				static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownObjective),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[OutdoorPvpHandler] CaptureStart OK account={} zid={} oid={} fac={}",
			accountId, parsed->zoneId, parsed->objectiveId,
			static_cast<unsigned>(parsed->faction));

		// Reponse Ok au requestor.
		auto okPkt = BuildOutdoorPvpCaptureStartResponsePacket(0u, requestId, sessionIdHeader);
		if (!okPkt.empty())
			m_server->Send(connId, okPkt);

		// V1 : push 3 progress notifications transitoires (25/50/75) au connId
		// initiateur. capturingBy = la faction qui capture.
		PushCaptureProgress(connId, parsed->zoneId, parsed->objectiveId,
			kV1ProgressStep1, parsed->faction);
		PushCaptureProgress(connId, parsed->zoneId, parsed->objectiveId,
			kV1ProgressStep2, parsed->faction);
		PushCaptureProgress(connId, parsed->zoneId, parsed->objectiveId,
			kV1ProgressStep3, parsed->faction);

		// Tick a 100% pour completer la capture (le manager remet capturePct=0,
		// transitionne owner et incremente le score). Recupere ensuite scores.
		bool captured = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			captured = m_manager.TickCapture(parsed->zoneId, parsed->objectiveId, kV1ProgressStep4);
			allianceScore = m_manager.Score(parsed->zoneId, 0u);
			hordeScore    = m_manager.Score(parsed->zoneId, 1u);
			if (captured)
				newOwner = parsed->faction;
		}

		if (!captured)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] TickCapture(100) returned false (unexpected) account={} zid={} oid={}",
				accountId, parsed->zoneId, parsed->objectiveId);
		}

		// Wave 5 : persiste la transition owner + scores apres capture.
		// Best-effort hors mutex.
		if (captured && m_store && m_store->IsAvailable())
		{
			engine::server::outdoorpvp_db::ObjectiveRow obj;
			obj.zoneId      = parsed->zoneId;
			obj.objectiveId = parsed->objectiveId;
			obj.owner       = newOwner;
			obj.capturePct  = 0u; // TickCapture remet a 0 apres transition.
			obj.capturingBy = 0xFFu;
			(void)m_store->UpsertObjective(obj);

			engine::server::outdoorpvp_db::ScoreRow sA;
			sA.zoneId  = parsed->zoneId;
			sA.faction = 0u;
			sA.score   = allianceScore;
			(void)m_store->UpsertScore(sA);

			engine::server::outdoorpvp_db::ScoreRow sH;
			sH.zoneId  = parsed->zoneId;
			sH.faction = 1u;
			sH.score   = hordeScore;
			(void)m_store->UpsertScore(sH);
		}

		// Push CompletedNotification avec nouveau owner et scores finaux.
		PushCaptureCompleted(connId, parsed->zoneId, parsed->objectiveId,
			newOwner, allianceScore, hordeScore);
	}

	// -------------------------------------------------------------------------
	// Push helpers
	// -------------------------------------------------------------------------

	bool OutdoorPvpHandler::PushCaptureProgress(uint32_t connId, uint32_t zoneId, uint32_t objectiveId,
		uint32_t capturePct, uint8_t capturingBy)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureProgress dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureProgress: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildOutdoorPvpCaptureProgressNotificationPacket(
			zoneId, objectiveId, capturePct, capturingBy, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureProgress: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[OutdoorPvpHandler] PushCaptureProgress connId={} zid={} oid={} pct={} fac={}",
			connId, zoneId, objectiveId, capturePct, static_cast<unsigned>(capturingBy));
		return true;
	}

	bool OutdoorPvpHandler::PushCaptureCompleted(uint32_t connId, uint32_t zoneId, uint32_t objectiveId,
		uint8_t newOwner, uint32_t allianceScore, uint32_t hordeScore)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureCompleted dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureCompleted: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildOutdoorPvpCaptureCompletedNotificationPacket(
			zoneId, objectiveId, newOwner, allianceScore, hordeScore, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[OutdoorPvpHandler] PushCaptureCompleted: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[OutdoorPvpHandler] PushCaptureCompleted connId={} zid={} oid={} owner={} a={} h={}",
			connId, zoneId, objectiveId, static_cast<unsigned>(newOwner),
			allianceScore, hordeScore);
		return true;
	}

	// -------------------------------------------------------------------------

	uint64_t OutdoorPvpHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
