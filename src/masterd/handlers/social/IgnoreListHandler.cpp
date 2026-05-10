// CMANGOS.25 (Phase 3.25 step 3+4) — Implementation IgnoreListHandler.

#include "src/masterd/handlers/social/IgnoreListHandler.h"

#include "src/masterd/social/IgnoreList.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/IgnoreListPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::server
{
	namespace
	{
		/// Convertit un IgnoreOpResult (cote manager) en IgnoreOpErrorCode (cote wire).
		uint8_t MapIgnoreError(engine::server::social::IgnoreOpResult r)
		{
			using engine::server::social::IgnoreOpResult;
			using engine::network::IgnoreOpErrorCode;
			switch (r)
			{
			case IgnoreOpResult::OK:             return static_cast<uint8_t>(IgnoreOpErrorCode::Ok);
			case IgnoreOpResult::AlreadyIgnored: return static_cast<uint8_t>(IgnoreOpErrorCode::AlreadyIgnored);
			case IgnoreOpResult::NotIgnored:     return static_cast<uint8_t>(IgnoreOpErrorCode::NotIgnored);
			case IgnoreOpResult::ListFull:       return static_cast<uint8_t>(IgnoreOpErrorCode::ListFull);
			case IgnoreOpResult::SelfIgnore:     return static_cast<uint8_t>(IgnoreOpErrorCode::SelfIgnore);
			}
			return static_cast<uint8_t>(IgnoreOpErrorCode::NotIgnored);
		}
	}

	void IgnoreListHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_mgr || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[IgnoreListHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account. Si l'un manque, on retourne une reponse
		// type-specific avec error=Unauthorized.
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
			const uint8_t kUnauth = static_cast<uint8_t>(IgnoreOpErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeIgnoreAddRequest:
				pkt = BuildIgnoreAddResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeIgnoreRemoveRequest:
				pkt = BuildIgnoreRemoveResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeIgnoreListRequest:
				pkt = BuildIgnoreListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
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
		case kOpcodeIgnoreAddRequest:
			HandleAdd(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeIgnoreRemoveRequest:
			HandleRemove(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeIgnoreListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void IgnoreListHandler::HandleAdd(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseIgnoreAddRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			// Payload malforme : on retourne un AlreadyIgnored generique avec
			// targetAccountId=0. Pas une vraie info utile, mais coherent avec
			// le reste des handlers (un Unauthorized serait moins precis).
			auto pkt = BuildIgnoreAddResponsePacket(
				static_cast<uint8_t>(IgnoreOpErrorCode::NotIgnored), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const auto opErr = m_mgr->Ignore(accountId, parsed->targetAccountId);
		const uint8_t code = MapIgnoreError(opErr);

		LOG_INFO(Net, "[IgnoreListHandler] Add owner={} target={} code={}",
			accountId, parsed->targetAccountId, static_cast<int>(code));

		auto pkt = BuildIgnoreAddResponsePacket(code, parsed->targetAccountId,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void IgnoreListHandler::HandleRemove(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseIgnoreRemoveRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildIgnoreRemoveResponsePacket(
				static_cast<uint8_t>(IgnoreOpErrorCode::NotIgnored), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const auto opErr = m_mgr->Unignore(accountId, parsed->targetAccountId);
		const uint8_t code = MapIgnoreError(opErr);

		LOG_INFO(Net, "[IgnoreListHandler] Remove owner={} target={} code={}",
			accountId, parsed->targetAccountId, static_cast<int>(code));

		auto pkt = BuildIgnoreRemoveResponsePacket(code, parsed->targetAccountId,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void IgnoreListHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		const auto ids = m_mgr->List(accountId);
		LOG_INFO(Net, "[IgnoreListHandler] List owner={} count={}", accountId, ids.size());

		auto pkt = BuildIgnoreListResponsePacket(0u, ids, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
