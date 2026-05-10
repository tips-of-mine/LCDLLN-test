// CMANGOS.33 (Phase 5.33 step 3+4) — Implementation LfgHandler.

#include "src/masterd/handlers/lfg/LfgHandler.h"

#include "src/masterd/lfg/LfgQueue.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/LfgPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Retourne le timestamp UTC en ms (epoch). Sert au joinedTsMs et a
		/// l'horodatage du tick matchmaking.
		uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}

		/// Estimee de temps d'attente en V1. Hardcode 60s — sera calcule
		/// statistiquement plus tard (cf. CLAUDE.md / spec).
		constexpr uint32_t kV1EstimatedWaitSec = 60u;
	}

	void LfgHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_queue || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[LfgHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(LfgErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeLfgQueueRequest:
				pkt = BuildLfgQueueResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeLfgLeaveRequest:
				pkt = BuildLfgLeaveResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeLfgStatusRequest:
				pkt = BuildLfgStatusResponsePacket(kUnauth, false, 0u, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeLfgMatchAcceptRequest:
				// V1 simplifie : reuse QueueResponse pour signaler Unauthorized
				// sur un MatchAccept (cote client, OnMatchAcceptResponse n'existe
				// pas — le client lit lastErrorText via OnQueueResponse).
				pkt = BuildLfgQueueResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
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
		case kOpcodeLfgQueueRequest:
			HandleQueue(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeLfgLeaveRequest:
			HandleLeave(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeLfgStatusRequest:
			HandleStatus(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeLfgMatchAcceptRequest:
			HandleMatchAccept(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void LfgHandler::HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseLfgQueueRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildLfgQueueResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::InvalidDungeon), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[LfgHandler] Queue parse failed account={}", accountId);
			return;
		}

		// Validation role : 0..2 (Tank / Healer / Damage).
		if (parsed->role > 2u)
		{
			auto pkt = BuildLfgQueueResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::InvalidRole), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[LfgHandler] Queue InvalidRole account={} role={}",
				accountId, static_cast<unsigned>(parsed->role));
			return;
		}

		// Validation dungeon : 0 = invalide en V1 (pas de catalogue cote master).
		if (parsed->dungeonId == 0u)
		{
			auto pkt = BuildLfgQueueResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::InvalidDungeon), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[LfgHandler] Queue InvalidDungeon account={}", accountId);
			return;
		}

		// Already-queued : si l'account a deja une entree active, on refuse.
		auto it = m_active.find(accountId);
		if (it != m_active.end())
		{
			auto pkt = BuildLfgQueueResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::AlreadyQueued), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_INFO(Net, "[LfgHandler] Queue AlreadyQueued account={} dungeon={}",
				accountId, it->second.dungeonId);
			return;
		}

		const uint64_t nowMs = NowMs();
		const auto role = static_cast<engine::server::lfg::LfgRole>(parsed->role);
		m_queue->Join(parsed->dungeonId, accountId, role, nowMs);

		ActiveEntry entry;
		entry.dungeonId  = parsed->dungeonId;
		entry.role       = parsed->role;
		entry.joinedTsMs = nowMs;
		m_active[accountId] = entry;

		LOG_INFO(Net, "[LfgHandler] Queue account={} dungeon={} role={}",
			accountId, parsed->dungeonId, static_cast<unsigned>(parsed->role));

		auto pkt = BuildLfgQueueResponsePacket(0u, kV1EstimatedWaitSec,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void LfgHandler::HandleLeave(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		auto it = m_active.find(accountId);
		if (it == m_active.end())
		{
			auto pkt = BuildLfgLeaveResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::NotInQueue),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_INFO(Net, "[LfgHandler] Leave NotInQueue account={}", accountId);
			return;
		}

		const uint32_t dungeonId = it->second.dungeonId;
		const bool removed = m_queue->Leave(dungeonId, accountId);
		(void)removed;
		m_active.erase(it);

		LOG_INFO(Net, "[LfgHandler] Leave account={} dungeon={}", accountId, dungeonId);

		auto pkt = BuildLfgLeaveResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void LfgHandler::HandleStatus(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		auto it = m_active.find(accountId);
		if (it == m_active.end())
		{
			auto pkt = BuildLfgStatusResponsePacket(0u, false, 0u, 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_DEBUG(Net, "[LfgHandler] Status notInQueue account={}", accountId);
			return;
		}

		const uint64_t nowMs = NowMs();
		const uint64_t elapsedMs = (nowMs > it->second.joinedTsMs) ? (nowMs - it->second.joinedTsMs) : 0u;
		const uint32_t elapsedSec = static_cast<uint32_t>(elapsedMs / 1000u);

		auto pkt = BuildLfgStatusResponsePacket(0u, true, it->second.role,
			it->second.dungeonId, elapsedSec, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		LOG_DEBUG(Net, "[LfgHandler] Status inQueue account={} dungeon={} elapsed={}",
			accountId, it->second.dungeonId, elapsedSec);
	}

	void LfgHandler::HandleMatchAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseLfgMatchAcceptRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildLfgQueueResponsePacket(
				static_cast<uint8_t>(LfgErrorCode::MatchExpired), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[LfgHandler] MatchAccept parse failed account={}", accountId);
			return;
		}

		// V1 simplifie : pas d'etat proposal cote master. On logue, on considere
		// le match comme "consume" (le compte n'est plus en queue cote handler
		// car il a ete retire au TickMatchmaking precedent), et on retourne
		// LfgQueueResponse Ok pour que le client puisse fermer son modal.
		// Si accept == false, on logue mais on ne re-queue pas le joueur (V1).
		LOG_INFO(Net, "[LfgHandler] MatchAccept account={} proposalId={} accept={}",
			accountId, parsed->proposalId, parsed->accept ? 1 : 0);

		auto pkt = BuildLfgQueueResponsePacket(0u, 0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	void LfgHandler::TickMatchmaking(uint64_t nowMs)
	{
		using namespace engine::network;
		(void)nowMs;
		if (!m_queue || !m_server) return;

		// Collecte des dungeons « connus » via l'index local m_active. On itere
		// les entries actives, on note les dungeons distincts, puis on tente
		// TryMatch sur chacun. Si un groupe est forme, on retire les membres
		// de m_active et on push une notification a chaque membre online.
		std::unordered_set<uint32_t> dungeons;
		for (const auto& [accId, entry] : m_active)
		{
			(void)accId;
			dungeons.insert(entry.dungeonId);
		}

		for (uint32_t dungeon : dungeons)
		{
			while (true)
			{
				auto group = m_queue->TryMatch(dungeon);
				if (!group) break;

				const uint64_t proposalId = m_nextProposalId++;
				std::vector<LfgMatchMember> members;
				members.reserve(group->members.size());
				for (uint64_t memberId : group->members)
				{
					LfgMatchMember m;
					m.accountId = memberId;
					// Recuperer le role depuis m_active avant de le clear.
					auto memberIt = m_active.find(memberId);
					m.role = (memberIt != m_active.end()) ? memberIt->second.role : 0u;
					members.push_back(m);
				}

				// Retire les membres de m_active.
				for (uint64_t memberId : group->members)
				{
					m_active.erase(memberId);
				}

				LOG_INFO(Net, "[LfgHandler] TickMatchmaking proposal={} dungeon={} members={}",
					proposalId, dungeon, group->members.size());

				// Push notification a chaque membre online.
				for (const auto& m : members)
				{
					const uint32_t targetConn = FindConnIdForAccount(m.accountId);
					const uint64_t targetSessionIdHeader = FindSessionIdForAccount(m.accountId);
					if (targetConn == 0u || targetSessionIdHeader == 0u)
					{
						LOG_WARN(Net, "[LfgHandler] Match member offline account={} (skip push)",
							m.accountId);
						continue;
					}
					auto pkt = BuildLfgMatchProposalNotificationPacket(
						proposalId, dungeon, members, targetSessionIdHeader);
					if (!pkt.empty())
						m_server->Send(targetConn, pkt);
				}
			}
		}
	}

	// -------------------------------------------------------------------------

	uint32_t LfgHandler::FindConnIdForAccount(uint64_t accountId) const
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

	uint64_t LfgHandler::FindSessionIdForAccount(uint64_t accountId) const
	{
		if (!m_connMap || !m_sessionMgr) return 0u;
		const auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessionId] : snapshot)
		{
			(void)connId;
			auto acc = m_sessionMgr->GetAccountId(sessionId);
			if (acc && *acc == accountId)
				return sessionId;
		}
		return 0u;
	}
}
