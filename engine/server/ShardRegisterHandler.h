#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class ShardRegistry;
}

namespace engine::server
{
	/// Handles SHARD_REGISTER and SHARD_HEARTBEAT opcodes on the Master. Updates ShardRegistry and sends REGISTER_OK/ERROR.
	class ShardRegisterHandler
	{
	public:
		ShardRegisterHandler() = default;

		void SetServer(NetServer* server);
		void SetShardRegistry(ShardRegistry* registry);

		/// Handles SHARD_REGISTER and SHARD_HEARTBEAT. Ignores other opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		void HandleRegister(uint32_t connId, uint32_t requestId, const uint8_t* payload, size_t payloadSize);
		void HandleHeartbeat(uint32_t connId, const uint8_t* payload, size_t payloadSize);

		NetServer* m_server = nullptr;
		ShardRegistry* m_registry = nullptr;
	};
}
