// CMANGOS.24 (Phase 3.24 step 3+4) — Implementation ReputationHandler.

#include "src/masterd/handlers/reputation/ReputationHandler.h"

#include "src/masterd/reputation/MysqlReputationStore.h"
#include "src/masterd/reputation/ReputationManager.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ReputationPayloads.h"

namespace engine::server
{
	void ReputationHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_mgr || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[ReputationHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(ReputationErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeReputationListRequest:
				pkt = BuildReputationListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
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
		case kOpcodeReputationListRequest:
			HandleListRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void ReputationHandler::HandleListRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		using engine::server::reputation::ReputationManager;
		using engine::server::reputation::ReputationStanding;

		// Le manager n'expose pas d'iteration directe sur les factions d'un account ;
		// on utilise donc le store comme source de verite a la List : si le store
		// est present, on Load depuis la DB (qui contient toutes les factions
		// connues du compte) puis on hydrate le manager (best-effort) pour les
		// futures requetes. Si pas de store, on retourne une liste vide
		// (V1 acceptable — le manager sera populate au fur et a mesure des gains).
		std::vector<ReputationEntry> entries;
		if (m_store)
		{
			const auto rows = m_store->Load(accountId);
			entries.reserve(rows.size());
			for (const auto& row : rows)
			{
				ReputationEntry e;
				e.factionId = row.factionId;
				e.value     = row.value;
				e.standing  = static_cast<int8_t>(ReputationManager::StandingFor(row.value));
				entries.push_back(e);
			}
		}

		LOG_INFO(Net, "[ReputationHandler] List account={} count={}", accountId, entries.size());

		auto pkt = BuildReputationListResponsePacket(0u, entries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	bool ReputationHandler::ApplyReputationDelta(uint64_t accountId, uint32_t factionId, int32_t delta)
	{
		using namespace engine::network;
		using engine::server::reputation::ReputationManager;

		if (!m_mgr)
		{
			LOG_WARN(Net, "[ReputationHandler] ApplyReputationDelta : no manager (account={}, faction={})",
				accountId, factionId);
			return false;
		}

		// Applique au manager (le manager applique aussi le spillover automatiquement).
		m_mgr->GainReputation(accountId, factionId, delta);
		const int32_t newValue = m_mgr->GetReputation(accountId, factionId);
		const auto    standing = ReputationManager::StandingFor(newValue);
		const int8_t  standingByte = static_cast<int8_t>(standing);

		// Persistance best-effort.
		if (m_store)
		{
			if (!m_store->Upsert(accountId, factionId, newValue))
			{
				LOG_WARN(Net, "[ReputationHandler] Upsert failed (account={}, faction={}, value={})",
					accountId, factionId, newValue);
			}
		}

		LOG_INFO(Net, "[ReputationHandler] ApplyDelta account={} faction={} delta={} newValue={} standing={}",
			accountId, factionId, delta, newValue, static_cast<int>(standingByte));

		// Push notification au client si online (seulement la faction primaire en V1).
		const uint32_t targetConn = FindConnIdForAccount(accountId);
		const uint64_t targetSessionIdHeader = FindSessionIdForAccount(accountId);
		if (targetConn != 0u && targetSessionIdHeader != 0u && m_server)
		{
			auto pkt = BuildReputationUpdateNotificationPacket(
				factionId, newValue, standingByte, delta, targetSessionIdHeader);
			if (!pkt.empty())
				m_server->Send(targetConn, pkt);
		}

		return true;
	}

	// -------------------------------------------------------------------------

	uint32_t ReputationHandler::FindConnIdForAccount(uint64_t accountId) const
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

	uint64_t ReputationHandler::FindSessionIdForAccount(uint64_t accountId) const
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
