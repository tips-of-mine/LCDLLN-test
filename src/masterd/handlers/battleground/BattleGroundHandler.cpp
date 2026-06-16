// CMANGOS.10 (Phase 5 step 3+4) — Implementation BattleGroundHandler.

#include "src/masterd/handlers/battleground/BattleGroundHandler.h"

#include "src/masterd/battleground/MysqlBattleGroundStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/BattleGroundPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <vector>

namespace engine::server
{
	// -------------------------------------------------------------------------
	// Helpers static
	// -------------------------------------------------------------------------

	BattleGroundHandler::BgInfoEntry BattleGroundHandler::GetBgInfoStatic(uint16_t bgType)
	{
		BgInfoEntry e;
		switch (bgType)
		{
		case kBgWarsong:
			e.bgType   = kBgWarsong;
			e.name     = "Gorge de Feyhin";
			e.teamSize = 10u;
			e.mapName  = "gorge_feyhin";
			break;
		case kBgArathi:
			e.bgType   = kBgArathi;
			e.name     = "Bassin des Ombres";
			e.teamSize = 15u;
			e.mapName  = "bassin_ombres";
			break;
		case kBgAlterac:
			e.bgType   = kBgAlterac;
			e.name     = "Vallee Gelee";
			e.teamSize = 40u;
			e.mapName  = "vallee_gelee";
			break;
		default:
			break;
		}
		return e;
	}

	bool BattleGroundHandler::IsValidBgType(uint16_t bgType)
	{
		return bgType == kBgWarsong || bgType == kBgArathi || bgType == kBgAlterac;
	}

	bool BattleGroundHandler::IsValidFaction(uint8_t faction)
	{
		return faction == 0u || faction == 1u;
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void BattleGroundHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[BattleGroundHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(BgErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeBgListRequest:
				pkt = BuildBgListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeBgQueueRequest:
				pkt = BuildBgQueueResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeBgLeaveQueueRequest:
				pkt = BuildBgLeaveQueueResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeBgLeaveMatchRequest:
				// Pas de Response paire (fire-and-forget V1) — drop silencieux.
				LOG_INFO(Net, "[BattleGroundHandler] LeaveMatch dropped : Unauthorized");
				return;
			default:
				return;
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeBgListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeBgQueueRequest:
			HandleQueue(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeBgLeaveQueueRequest:
			HandleLeaveQueue(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeBgLeaveMatchRequest:
			HandleLeaveMatch(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// HandleList
	// -------------------------------------------------------------------------

	void BattleGroundHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<BgInfo> bgs;
		bgs.reserve(3u);
		const uint16_t types[] = {kBgWarsong, kBgArathi, kBgAlterac};
		for (uint16_t t : types)
		{
			auto e = GetBgInfoStatic(t);
			BgInfo bg;
			bg.bgType   = e.bgType;
			bg.name     = e.name;
			bg.teamSize = e.teamSize;
			bg.mapName  = e.mapName;
			bgs.push_back(std::move(bg));
		}

		LOG_INFO(Net, "[BattleGroundHandler] List account={} count={}",
			accountId, bgs.size());

		auto pkt = BuildBgListResponsePacket(0u, bgs, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleQueue — V1 : queue ack + match V1 vs AI bot immediatement.
	// -------------------------------------------------------------------------

	void BattleGroundHandler::HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseBgQueueRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[BattleGroundHandler] Queue parse failed account={}", accountId);
			auto pkt = BuildBgQueueResponsePacket(
				static_cast<uint8_t>(BgErrorCode::UnknownBg), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation bgType.
		if (!IsValidBgType(parsed->bgType))
		{
			LOG_INFO(Net, "[BattleGroundHandler] Queue UnknownBg account={} bgType={}",
				accountId, static_cast<unsigned>(parsed->bgType));
			auto pkt = BuildBgQueueResponsePacket(
				static_cast<uint8_t>(BgErrorCode::UnknownBg), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation faction.
		if (!IsValidFaction(parsed->faction))
		{
			LOG_INFO(Net, "[BattleGroundHandler] Queue InvalidFaction account={} faction={}",
				accountId, static_cast<unsigned>(parsed->faction));
			auto pkt = BuildBgQueueResponsePacket(
				static_cast<uint8_t>(BgErrorCode::InvalidFaction), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Decide ce qui se passe sous mutex : already queued ? deja en match ? sinon
		// inscrire en queue ET creer match V1 ET pop la queue.
		uint64_t newMatchId = 0;
		bool alreadyQueued = false;
		bool alreadyInMatch = false;
		uint8_t allianceCount = 0;
		uint8_t hordeCount = 0;
		uint8_t winnerFaction = 0;
		BgInfoEntry bgInfo;
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			if (m_queue.find(accountId) != m_queue.end())
			{
				alreadyQueued = true;
			}
			else if (m_accountMatch.find(accountId) != m_accountMatch.end())
			{
				// Considere comme "deja en queue" cote wire : V1 simplifie. On
				// remappe sur AlreadyQueued pour ne pas inventer un nouveau code.
				alreadyInMatch = true;
			}
			else
			{
				bgInfo = GetBgInfoStatic(parsed->bgType);

				// Inscrit en queue (pour traceabilite log/metrics ; V1 retire
				// immediatement apres puisque le match part de suite).
				QueueState qs;
				qs.bgType   = parsed->bgType;
				qs.faction  = parsed->faction;
				qs.queuedAt = std::chrono::steady_clock::now();
				m_queue[accountId] = qs;

				// Cree le match V1 immediatement.
				newMatchId = m_nextMatchId.fetch_add(1ull);
				ActiveMatch m;
				m.bgType     = parsed->bgType;
				m.mapName    = bgInfo.mapName;
				m.startedAt  = std::chrono::steady_clock::now();
				m.allianceScore = 0u;
				m.hordeScore    = 0u;
				if (parsed->faction == 0u)
				{
					m.alliance.push_back(accountId);
					m.horde.push_back(kAiBotAccountId);
				}
				else
				{
					m.alliance.push_back(kAiBotAccountId);
					m.horde.push_back(accountId);
				}
				allianceCount = static_cast<uint8_t>(m.alliance.size());
				hordeCount    = static_cast<uint8_t>(m.horde.size());

				m_matches[newMatchId] = m;
				m_accountMatch[accountId] = newMatchId;

				// Pop la queue (V1 : queue est juste un ack, le match part de suite).
				m_queue.erase(accountId);

				// V1 : winnerFaction tirage 50/50 entre joueur et opposite.
				if (!m_rngSeeded)
				{
					const auto seed = static_cast<std::mt19937::result_type>(
						std::chrono::steady_clock::now().time_since_epoch().count());
					m_rng.seed(seed);
					m_rngSeeded = true;
				}
				std::uniform_int_distribution<uint32_t> dist(0u, 1u);
				const uint32_t coin = dist(m_rng);
				// coin == 0 : le joueur gagne, coin == 1 : le opposite gagne.
				if (coin == 0u)
					winnerFaction = parsed->faction; // 0=Alliance, 1=Horde
				else
					winnerFaction = (parsed->faction == 0u) ? 1u : 0u;
			}
		}

		if (alreadyQueued || alreadyInMatch)
		{
			LOG_INFO(Net, "[BattleGroundHandler] Queue AlreadyQueued account={} (inMatch={})",
				accountId, alreadyInMatch ? 1 : 0);
			auto pkt = BuildBgQueueResponsePacket(
				static_cast<uint8_t>(BgErrorCode::AlreadyQueued), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[BattleGroundHandler] Queue OK account={} bgType={} faction={} matchId={}",
			accountId, static_cast<unsigned>(parsed->bgType),
			static_cast<unsigned>(parsed->faction), newMatchId);

		// Envoie le QueueResponse OK.
		auto qPkt = BuildBgQueueResponsePacket(0u, kV1EstimatedWaitSec, kV1QueuePosition,
			requestId, sessionIdHeader);
		if (!qPkt.empty())
			m_server->Send(connId, qPkt);

		// V1 : pousse immediatement la sequence Start -> Score(s) -> End.
		PushMatchStart(connId, newMatchId, parsed->bgType, bgInfo.mapName,
			allianceCount, hordeCount);

		// 3 score updates simules (5 / 10 / 15 sec). Faux scores qui montent
		// progressivement vers le total final.
		PushScoreUpdate(connId, newMatchId, 200u, 100u, kV1ScoreElapsed1);
		PushScoreUpdate(connId, newMatchId, 500u, 350u, kV1ScoreElapsed2);
		PushScoreUpdate(connId, newMatchId, 900u, 800u, kV1ScoreElapsed3);

		// Final scores : 1500 vs 1200 si gagnant Alliance ; sinon 1200 vs 1500.
		uint32_t finalAlliance = 0u;
		uint32_t finalHorde = 0u;
		if (winnerFaction == 0u)
		{
			finalAlliance = 1500u;
			finalHorde    = 1200u;
		}
		else if (winnerFaction == 1u)
		{
			finalAlliance = 1200u;
			finalHorde    = 1500u;
		}
		else
		{
			finalAlliance = 1300u;
			finalHorde    = 1300u;
		}

		PushMatchEnd(connId, newMatchId, winnerFaction, finalAlliance, finalHorde, kV1MatchDuration);

		// Wave 5 (Phase 5.10b) : archive le match en DB (best-effort).
		// Le startedAtUnixMs est recalcule a partir du chrono in-memory :
		// system_clock::now() - kV1MatchDuration secondes, approximation
		// suffisante V1 (le match est joue en quasi-instantane cote master).
		if (m_historyStore && m_historyStore->IsAvailable())
		{
			const auto nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			const uint64_t startedAtMs = (nowMs > kV1MatchDuration * 1000ull)
				? (nowMs - kV1MatchDuration * 1000ull) : 0ull;
			m_historyStore->InsertMatch(parsed->bgType, bgInfo.mapName,
				finalAlliance, finalHorde, winnerFaction,
				kV1MatchDuration, startedAtMs);
		}

		// Cleanup : retire le match cote master apres push de End. Le client
		// gere la disparition cote UI quand il recoit le End.
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_matches.find(newMatchId);
			if (it != m_matches.end())
			{
				it->second.allianceScore = finalAlliance;
				it->second.hordeScore    = finalHorde;
				m_matches.erase(it);
			}
			m_accountMatch.erase(accountId);
		}
	}

	// -------------------------------------------------------------------------
	// HandleLeaveQueue
	// -------------------------------------------------------------------------

	void BattleGroundHandler::HandleLeaveQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		bool wasQueued = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_queue.find(accountId);
			if (it != m_queue.end())
			{
				wasQueued = true;
				m_queue.erase(it);
			}
		}

		if (!wasQueued)
		{
			LOG_INFO(Net, "[BattleGroundHandler] LeaveQueue NotInQueue account={}", accountId);
			auto pkt = BuildBgLeaveQueueResponsePacket(
				static_cast<uint8_t>(BgErrorCode::NotInQueue),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[BattleGroundHandler] LeaveQueue OK account={}", accountId);
		auto pkt = BuildBgLeaveQueueResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleLeaveMatch — fire-and-forget : push MatchEnd (forfait), pas de
	// Response paire.
	// -------------------------------------------------------------------------

	void BattleGroundHandler::HandleLeaveMatch(uint32_t connId, uint32_t /*requestId*/, uint64_t /*sessionIdHeader*/,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		uint64_t matchId = 0;
		uint16_t bgType = 0;
		bool found = false;
		uint8_t opposite = 2u; // par defaut Draw si on ne sait pas
		uint32_t allianceScore = 0u;
		uint32_t hordeScore    = 0u;
		uint32_t durationSec   = 0u;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto idxIt = m_accountMatch.find(accountId);
			if (idxIt != m_accountMatch.end())
			{
				matchId = idxIt->second;
				auto mIt = m_matches.find(matchId);
				if (mIt != m_matches.end())
				{
					found = true;
					bgType = mIt->second.bgType;
					// Determine la faction du joueur (Alliance ou Horde) pour
					// donner la victoire a l'opposite.
					bool isAlliance = false;
					for (uint64_t a : mIt->second.alliance)
					{
						if (a == accountId) { isAlliance = true; break; }
					}
					opposite = isAlliance ? 1u /*Horde win*/ : 0u /*Alliance win*/;
					allianceScore = mIt->second.allianceScore;
					hordeScore    = mIt->second.hordeScore;
					const auto now = std::chrono::steady_clock::now();
					durationSec = static_cast<uint32_t>(
						std::chrono::duration_cast<std::chrono::seconds>(
							now - mIt->second.startedAt).count());
					m_matches.erase(mIt);
				}
				m_accountMatch.erase(idxIt);
			}
		}

		if (!found)
		{
			LOG_INFO(Net, "[BattleGroundHandler] LeaveMatch no active match account={} (drop)",
				accountId);
			return;
		}

		LOG_INFO(Net, "[BattleGroundHandler] LeaveMatch forfeit account={} matchId={} bgType={} winner={}",
			accountId, matchId, static_cast<unsigned>(bgType), static_cast<unsigned>(opposite));

		// V1 : push MatchEnd avec winnerFaction = opposite. Pas de Response
		// paire (fire-and-forget).
		PushMatchEnd(connId, matchId, opposite, allianceScore, hordeScore, durationSec);
	}

	// -------------------------------------------------------------------------
	// Push helpers
	// -------------------------------------------------------------------------

	bool BattleGroundHandler::PushMatchStart(uint32_t connId, uint64_t matchId, uint16_t bgType,
		const std::string& mapName, uint8_t allianceCount, uint8_t hordeCount)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchStart dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchStart: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildBgMatchStartNotificationPacket(
			matchId, bgType, mapName, allianceCount, hordeCount, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchStart: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[BattleGroundHandler] PushMatchStart connId={} matchId={} bgType={} a={} h={}",
			connId, matchId, static_cast<unsigned>(bgType),
			static_cast<unsigned>(allianceCount), static_cast<unsigned>(hordeCount));
		return true;
	}

	bool BattleGroundHandler::PushScoreUpdate(uint32_t connId, uint64_t matchId,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushScoreUpdate dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushScoreUpdate: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildBgScoreUpdateNotificationPacket(
			matchId, allianceScore, hordeScore, elapsedSec, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushScoreUpdate: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[BattleGroundHandler] PushScoreUpdate connId={} matchId={} a={} h={} t={}s",
			connId, matchId, allianceScore, hordeScore, elapsedSec);
		return true;
	}

	bool BattleGroundHandler::PushMatchEnd(uint32_t connId, uint64_t matchId, uint8_t winnerFaction,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchEnd dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchEnd: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildBgMatchEndNotificationPacket(
			matchId, winnerFaction, allianceScore, hordeScore, durationSec, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[BattleGroundHandler] PushMatchEnd: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[BattleGroundHandler] PushMatchEnd connId={} matchId={} winner={} a={} h={} dur={}s",
			connId, matchId, static_cast<unsigned>(winnerFaction),
			allianceScore, hordeScore, durationSec);
		return true;
	}

	// -------------------------------------------------------------------------

	uint64_t BattleGroundHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
