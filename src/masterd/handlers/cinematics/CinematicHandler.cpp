// CMANGOS.30 (Phase 5.30 step 3+4) — Implementation CinematicHandler.

#include "src/masterd/handlers/cinematics/CinematicHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/CinematicPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::server
{
	void CinematicHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[CinematicHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account — meme pattern que LfgHandler.
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
			const uint8_t kUnauth = static_cast<uint8_t>(CinematicErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeCinematicAckRequest:
				pkt = BuildCinematicAckResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeCinematicSkipRequest:
				pkt = BuildCinematicSkipResponsePacket(kUnauth, false, requestId, sessionIdHeader);
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
		case kOpcodeCinematicAckRequest:
			HandleAckRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeCinematicSkipRequest:
			HandleSkipRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void CinematicHandler::HandleAckRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseCinematicAckRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			// V1 : on log mais on repond Ok (le client n'a aucun moyen de
			// recover si on lui rejette son ack). NoActiveCinematic est plus
			// approprie comme code futur, mais on garde Ok ici pour ne pas
			// faire du noise cote client.
			auto pkt = BuildCinematicAckResponsePacket(0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[CinematicHandler] Ack parse failed account={} (responding Ok)", accountId);
			return;
		}

		LOG_INFO(Net, "[CinematicHandler] Ack account={} sequenceId={} completionState={}",
			accountId, parsed->sequenceId, static_cast<unsigned>(parsed->completionState));

		auto pkt = BuildCinematicAckResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void CinematicHandler::HandleSkipRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseCinematicSkipRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			// V1 : meme philosophie que Ack, on repond Ok+allowed pour debloquer
			// le client. Le futur catalog "non-skippable" pourra retourner
			// SkipNotAllowed proprement.
			auto pkt = BuildCinematicSkipResponsePacket(0u, true, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[CinematicHandler] Skip parse failed account={} (responding allowed)",
				accountId);
			return;
		}

		LOG_INFO(Net, "[CinematicHandler] Skip account={} sequenceId={} -> allowed=true (V1)",
			accountId, parsed->sequenceId);

		// V1 : skip toujours autorise. Future PR introduira un catalog
		// "non-skippable" cote master pour les cinematiques obligatoires
		// (intro, scenarios scriptes critiques).
		auto pkt = BuildCinematicSkipResponsePacket(0u, true, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	bool CinematicHandler::PushCinematic(uint64_t accountId, uint32_t sequenceId, uint8_t reason)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[CinematicHandler] PushCinematic dropped : handler not fully wired");
			return false;
		}

		const uint32_t targetConn = FindConnIdForAccount(accountId);
		const uint64_t targetSessionIdHeader = FindSessionIdForAccount(accountId);
		if (targetConn == 0u || targetSessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[CinematicHandler] PushCinematic: account={} offline (skip)", accountId);
			return false;
		}

		auto pkt = BuildCinematicPlayNotificationPacket(sequenceId, reason, targetSessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[CinematicHandler] PushCinematic: build packet failed account={}", accountId);
			return false;
		}

		m_server->Send(targetConn, pkt);
		LOG_INFO(Net, "[CinematicHandler] PushCinematic account={} sequenceId={} reason={}",
			accountId, sequenceId, static_cast<unsigned>(reason));
		return true;
	}

	// -------------------------------------------------------------------------

	uint32_t CinematicHandler::FindConnIdForAccount(uint64_t accountId) const
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

	uint64_t CinematicHandler::FindSessionIdForAccount(uint64_t accountId) const
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
