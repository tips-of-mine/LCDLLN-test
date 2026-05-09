// CMANGOS.23 (Phase 5.23 step 3+4) — Implementation QuestHandler.

#include "src/masterd/handlers/quest/QuestHandler.h"

#include "src/masterd/quests/MysqlQuestStateStore.h"
#include "src/masterd/quests/QuestState.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/QuestPayloads.h"

namespace engine::server
{
	namespace
	{
		/// Convertit un QuestOpResult (cote tracker) en QuestOpErrorCode (cote wire).
		uint8_t MapQuestError(engine::server::quests::QuestOpResult r)
		{
			using engine::server::quests::QuestOpResult;
			using engine::network::QuestOpErrorCode;
			switch (r)
			{
			case QuestOpResult::OK:            return static_cast<uint8_t>(QuestOpErrorCode::Ok);
			case QuestOpResult::WrongStatus:   return static_cast<uint8_t>(QuestOpErrorCode::WrongStatus);
			case QuestOpResult::QuestNotFound: return static_cast<uint8_t>(QuestOpErrorCode::QuestNotFound);
			}
			return static_cast<uint8_t>(QuestOpErrorCode::WrongStatus);
		}
	}

	void QuestHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_tracker || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[QuestHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account. Si l'un manque, on retourne une reponse
		// type-specific avec error=Unauthorized (le client peut alors prompter
		// une re-auth).
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
			const uint8_t kUnauth = static_cast<uint8_t>(QuestOpErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeQuestAcceptRequest:
				pkt = BuildQuestAcceptResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeQuestCompleteRequest:
				pkt = BuildQuestCompleteResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeQuestRewardRequest:
				pkt = BuildQuestRewardResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeQuestListRequest:
				pkt = BuildQuestListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
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
		case kOpcodeQuestAcceptRequest:
			HandleAccept(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeQuestCompleteRequest:
			HandleComplete(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeQuestRewardRequest:
			HandleReward(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeQuestListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void QuestHandler::HandleAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseQuestAcceptRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildQuestAcceptResponsePacket(
				static_cast<uint8_t>(QuestOpErrorCode::QuestNotFound), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const auto opErr = m_tracker->Accept(accountId, parsed->questId);
		uint8_t newStatus = static_cast<uint8_t>(m_tracker->Get(accountId, parsed->questId));
		const uint8_t code = MapQuestError(opErr);

		if (opErr == engine::server::quests::QuestOpResult::OK && m_store)
		{
			// Persistance best-effort. Si l'ecriture DB echoue, on logue mais on
			// ne rollback pas : le tracker memoire reste l'autorite runtime ;
			// la perte sera rattrapee lors d'un futur reload (PR ulterieure).
			if (!m_store->Upsert(accountId, parsed->questId,
				engine::server::quests::QuestStatus::Accepted))
			{
				LOG_WARN(Net, "[QuestHandler] Accept Upsert failed (account={}, quest={})",
					accountId, parsed->questId);
			}
		}

		LOG_INFO(Net, "[QuestHandler] Accept account={} quest={} code={} status={}",
			accountId, parsed->questId, static_cast<int>(code),
			static_cast<int>(newStatus));

		auto pkt = BuildQuestAcceptResponsePacket(code, parsed->questId, newStatus,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void QuestHandler::HandleComplete(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseQuestCompleteRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildQuestCompleteResponsePacket(
				static_cast<uint8_t>(QuestOpErrorCode::QuestNotFound), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const auto opErr = m_tracker->Complete(accountId, parsed->questId);
		const uint8_t newStatus = static_cast<uint8_t>(m_tracker->Get(accountId, parsed->questId));
		const uint8_t code = MapQuestError(opErr);

		if (opErr == engine::server::quests::QuestOpResult::OK && m_store)
		{
			if (!m_store->Upsert(accountId, parsed->questId,
				engine::server::quests::QuestStatus::Completed))
			{
				LOG_WARN(Net, "[QuestHandler] Complete Upsert failed (account={}, quest={})",
					accountId, parsed->questId);
			}
		}

		LOG_INFO(Net, "[QuestHandler] Complete account={} quest={} code={} status={}",
			accountId, parsed->questId, static_cast<int>(code),
			static_cast<int>(newStatus));

		auto pkt = BuildQuestCompleteResponsePacket(code, parsed->questId, newStatus,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void QuestHandler::HandleReward(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseQuestRewardRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildQuestRewardResponsePacket(
				static_cast<uint8_t>(QuestOpErrorCode::QuestNotFound), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// V1 : on bascule l'etat Completed -> Rewarded sans deposer effectivement
		// les recompenses dans l'inventaire. La feature reelle (xp, items, gold)
		// sera cablee dans une PR ulterieure une fois l'API inventaire stabilisee.
		// On retourne quand meme code=Ok pour ne pas bloquer le UI flow ; le
		// commentaire NotImplementedYet est exploite cote client si besoin.
		const auto opErr = m_tracker->Reward(accountId, parsed->questId);
		const uint8_t newStatus = static_cast<uint8_t>(m_tracker->Get(accountId, parsed->questId));
		const uint8_t code = MapQuestError(opErr);

		if (opErr == engine::server::quests::QuestOpResult::OK && m_store)
		{
			if (!m_store->Upsert(accountId, parsed->questId,
				engine::server::quests::QuestStatus::Rewarded))
			{
				LOG_WARN(Net, "[QuestHandler] Reward Upsert failed (account={}, quest={})",
					accountId, parsed->questId);
			}
		}

		LOG_INFO(Net, "[QuestHandler] Reward account={} quest={} code={} status={}",
			accountId, parsed->questId, static_cast<int>(code),
			static_cast<int>(newStatus));

		auto pkt = BuildQuestRewardResponsePacket(code, parsed->questId, newStatus,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void QuestHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		const auto states = m_tracker->ListAll(accountId);
		std::vector<QuestStateEntry> entries;
		entries.reserve(states.size());
		for (const auto& [qId, status] : states)
		{
			QuestStateEntry e;
			e.questId = qId;
			e.status  = static_cast<uint8_t>(status);
			entries.push_back(e);
		}

		LOG_INFO(Net, "[QuestHandler] List account={} count={}", accountId, entries.size());

		auto pkt = BuildQuestListResponsePacket(0u, entries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
