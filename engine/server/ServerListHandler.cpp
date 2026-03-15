#include "engine/server/ServerListHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardRegistry.h"
#include "engine/network/ServerListPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <vector>

namespace engine::server
{
	void ServerListHandler::SetServer(NetServer* server) { m_server = server; }
	void ServerListHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }
	void ServerListHandler::SetCharacterCountHook(std::function<uint32_t(uint32_t)> hook) { m_characterCountHook = std::move(hook); }

	void ServerListHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		if (opcode != kOpcodeServerListRequest)
			return;
		if (!m_server || !m_registry)
		{
			LOG_WARN(Core, "[ServerListHandler] HandlePacket: server or registry not set");
			return;
		}
		std::vector<engine::network::ServerListEntry> entries;
		{
			auto list = m_registry->ListShards();
			entries.reserve(list.size());
			for (const auto& s : list)
			{
				engine::network::ServerListEntry e;
				e.shard_id = s.shard_id;
				e.status = static_cast<uint8_t>(s.state);
				e.current_load = s.current_load;
				e.max_capacity = s.max_capacity;
				e.character_count = m_characterCountHook ? m_characterCountHook(s.shard_id) : 0u;
				e.endpoint = s.endpoint;
				entries.push_back(e);
			}
		}
		auto payload = BuildServerListResponsePayload(entries);
		if (payload.empty())
		{
			LOG_ERROR(Core, "[ServerListHandler] BuildServerListResponsePayload failed (connId={})", connId);
			return;
		}
		auto pkt = BuildServerListResponsePacket(requestId, payload);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Core, "[ServerListHandler] Send SERVER_LIST_RESPONSE failed (connId={})", connId);
			return;
		}
		LOG_INFO(Core, "[ServerListHandler] SERVER_LIST_RESPONSE sent (connId={} entries={})", connId, entries.size());
	}
}
