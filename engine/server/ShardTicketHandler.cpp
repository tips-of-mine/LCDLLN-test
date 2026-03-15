#include "engine/server/ShardTicketHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardRegistry.h"
#include "engine/server/SessionManager.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/ShardTicketCrypto.h"
#include "engine/network/ShardTicketPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/NetErrorCode.h"
#include "engine/core/Log.h"

#include <openssl/rand.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace engine::server
{
	void ShardTicketHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardTicketHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }
	void ShardTicketHandler::SetSessionManager(SessionManager* sessionManager) { m_sessionManager = sessionManager; }
	void ShardTicketHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connSessionMap = map; }
	void ShardTicketHandler::SetSecret(std::string secret) { m_secret = std::move(secret); }

	void ShardTicketHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		std::fprintf(stderr, "[TICKET_SRV] HandlePacket connId=%u opcode=%u\n", connId, opcode); std::fflush(stderr);
		if (opcode != kOpcodeRequestShardTicket)
			return;
		if (!m_server || !m_registry || !m_sessionManager || !m_connSessionMap)
		{
			LOG_WARN(Core, "[ShardTicketHandler] HandlePacket: dependencies not set");
			return;
		}
		if (m_secret.empty())
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_WARN(Core, "[ShardTicketHandler] REQUEST_SHARD_TICKET rejected: no secret (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "shard ticket not configured", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto sessionOpt = m_connSessionMap->GetSessionId(connId);
		if (!sessionOpt)
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_WARN(Core, "[ShardTicketHandler] REQUEST_SHARD_TICKET rejected: not authenticated (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "login required", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto accountOpt = m_sessionManager->GetAccountId(*sessionOpt);
		if (!accountOpt)
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_WARN(Core, "[ShardTicketHandler] REQUEST_SHARD_TICKET rejected: session invalid (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "session invalid", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto req = ParseRequestShardTicketPayload(payload, payloadSize);
		if (!req)
		{
			LOG_WARN(Core, "[ShardTicketHandler] REQUEST_SHARD_TICKET invalid payload (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto shardOpt = m_registry->GetShard(req->target_shard_id);
		if (!shardOpt || shardOpt->state != ShardState::Online)
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_WARN(Core, "[ShardTicketHandler] REQUEST_SHARD_TICKET shard unavailable (connId={} target={})", connId, req->target_shard_id);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "shard unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		engine::network::ShardTicketId ticket_id;
		if (RAND_bytes(ticket_id.data(), static_cast<int>(ticket_id.size())) != 1)
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_ERROR(Core, "[ShardTicketHandler] RAND_bytes failed (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "ticket creation failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		uint64_t now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		uint64_t expires_at = now_sec + static_cast<uint64_t>(m_validity_sec);
		std::array<uint8_t, engine::network::kShardTicketHmacSize> hmac{};
		if (!ComputeTicketHmac(ticket_id.data(), ticket_id.size(), *accountOpt, req->target_shard_id, expires_at,
			m_secret, hmac.data(), hmac.size()))
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_ERROR(Core, "[ShardTicketHandler] ComputeTicketHmac failed (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "ticket creation failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto ticketPayload = BuildShardTicketPayload(ticket_id, *accountOpt, req->target_shard_id, expires_at, hmac.data(), hmac.size());
		if (ticketPayload.empty())
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_ERROR(Core, "[ShardTicketHandler] BuildShardTicketPayload failed (connId={})", connId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "ticket creation failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto pkt = BuildShardTicketResponsePacket(requestId, ticketPayload);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
			LOG_ERROR(Core, "[ShardTicketHandler] Send SHARD_TICKET_RESPONSE failed (connId={})", connId);
			return;
		}
		std::fprintf(stderr, "[TICKET_SRV] ticket created account_id=%llu shard_id=%u\n",
			(unsigned long long)*accountOpt, req->target_shard_id); std::fflush(stderr);
		LOG_INFO(Core, "[ShardTicketHandler] Ticket issued (connId={} account_id={} target_shard_id={} expires_at={})",
			connId, *accountOpt, req->target_shard_id, expires_at);
	}
}
