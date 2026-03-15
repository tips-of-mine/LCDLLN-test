#include "engine/server/ShardTicketHandshakeHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardTicketValidator.h"
#include "engine/network/ShardTicketPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <cstdio>

namespace engine::server
{
	void ShardTicketHandshakeHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardTicketHandshakeHandler::SetValidator(ShardTicketValidator* validator) { m_validator = validator; }

	void ShardTicketHandshakeHandler::OnConnectionClosed(uint32_t connId)
	{
		m_handshakeDone.erase(connId);
	}

	void ShardTicketHandshakeHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		std::fprintf(stderr, "[TICKET_HS] HandlePacket connId=%u opcode=%u\n", connId, opcode); std::fflush(stderr);
		if (!m_server || !m_validator)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] HandlePacket: server or validator not set");
			return;
		}
		if (m_handshakeDone.count(connId) != 0)
			return;
		if (opcode != kOpcodePresentShardTicket)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] Conn {} first packet not PRESENT_SHARD_TICKET (opcode={}), rejecting", connId, opcode);
			auto pkt = BuildShardTicketRejectedPacket(requestId, "ticket required");
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			m_server->CloseConnection(connId, DisconnectReason::InvalidPacket);
			return;
		}
		auto accept = m_validator->VerifyAndConsume(payload, payloadSize);
		std::fprintf(stderr, "[TICKET_HS] VerifyAndConsume result=%s\n", accept ? "ACCEPTED" : "REJECTED"); std::fflush(stderr);
		if (!accept)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] Conn {} ticket invalid or expired, rejecting", connId);
			auto pkt = BuildShardTicketRejectedPacket(requestId, "invalid or expired ticket");
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			m_server->CloseConnection(connId, DisconnectReason::InvalidPacket);
			return;
		}
		auto pkt = BuildShardTicketAcceptedPacket(requestId);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Core, "[ShardTicketHandshakeHandler] Send TICKET_ACCEPTED failed (connId={})", connId);
			m_server->CloseConnection(connId, DisconnectReason::SendError);
			return;
		}
		m_handshakeDone.insert(connId);
		LOG_INFO(Core, "[ShardTicketHandshakeHandler] Ticket accepted (connId={} account_id={} target_shard_id={})", connId, accept->account_id, accept->target_shard_id);
	}
}
