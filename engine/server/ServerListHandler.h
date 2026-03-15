#pragma once

#include <cstdint>
#include <functional>

namespace engine::server
{
	class NetServer;
	class ShardRegistry;
}

namespace engine::server
{
	/// Handles SERVER_LIST_REQUEST on the Master; responds with list of shards (shard_id, status, load, capacity) and optional character count hook (M22.5).
	class ServerListHandler
	{
	public:
		ServerListHandler() = default;

		void SetServer(NetServer* server);
		void SetShardRegistry(ShardRegistry* registry);

		/// Optional: per-shard character count for display (M20/M21 tables). Called with shard_id; return 0 if not available.
		void SetCharacterCountHook(std::function<uint32_t(uint32_t shard_id)> hook);

		/// Handles SERVER_LIST_REQUEST only. Ignores other opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
		ShardRegistry* m_registry = nullptr;
		std::function<uint32_t(uint32_t)> m_characterCountHook;
	};
}
