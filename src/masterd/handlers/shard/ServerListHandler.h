#pragma once

#include <cstdint>
#include <functional>

namespace engine::server
{
	class NetServer;
	class ShardRegistry;
	class ServerRegistry;
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
		void SetServerRegistry(ServerRegistry* selfRegistry);

		/// Optional: nombre total de sessions actives cote master (sessionCharMap.Count()).
		/// Quand UN SEUL shard est online (cas dev/single-shard), on utilise cette valeur a
		/// la place de `current_load` (issu du heartbeat shard). Le shard binaire ne voit que
		/// les TCP transitoires (handshake ticket → close), donc son current_load reste a 0
		/// en pratique pendant que des joueurs sont en monde. Sans ce hook, le client affiche
		/// 0/500 alors que l'API HTTP `/status` (qui applique la meme logique) montre players=1.
		/// Si non cable, on retombe sur s.current_load pour toutes les entrees.
		void SetMasterSessionCountHook(std::function<uint32_t()> hook);

		/// Handles SERVER_LIST_REQUEST only. Ignores other opcodes.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
		ShardRegistry* m_registry = nullptr;
		ServerRegistry* m_serverRegistry = nullptr;
		std::function<uint32_t(uint32_t)> m_characterCountHook;
		std::function<uint32_t()> m_masterSessionCountHook;
	};
}
