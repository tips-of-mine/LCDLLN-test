#pragma once

#include <cstdint>
#include <unordered_set>

namespace engine::server
{
	class NetServer;
	class ShardTicketValidator;
}

namespace engine::server
{
	/// Shard-side handler: first packet from each connection must be PRESENT_SHARD_TICKET; validates and responds TICKET_ACCEPTED or TICKET_REJECTED (M22.6).
	class ShardTicketHandshakeHandler
	{
	public:
		ShardTicketHandshakeHandler() = default;

		void SetServer(NetServer* server);
		void SetValidator(ShardTicketValidator* validator);

		/// Handles PRESENT_SHARD_TICKET only for connections that have not yet completed handshake. Others are ignored.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

		/// Call when a connection is closed (optional cleanup). If not called, set may grow.
		void OnConnectionClosed(uint32_t connId);

	private:
		NetServer* m_server = nullptr;
		ShardTicketValidator* m_validator = nullptr;
		std::unordered_set<uint32_t> m_handshakeDone;
	};
}
