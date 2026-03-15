#include "engine/server/ShardRegisterHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardRegistry.h"
#include "engine/network/ShardPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <cstdio>

namespace engine::server
{
	void ShardRegisterHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardRegisterHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }

	void ShardRegisterHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (!m_server || !m_registry)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] HandlePacket: server or registry not set");
			return;
		}
		if (opcode == kOpcodeShardRegister)
		{
			HandleRegister(connId, requestId, payload, payloadSize);
			return;
		}
		if (opcode == kOpcodeShardHeartbeat)
		{
			HandleHeartbeat(connId, payload, payloadSize);
			return;
		}
	}

	void ShardRegisterHandler::HandleRegister(uint32_t connId, uint32_t requestId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		std::fprintf(stderr, "[SREG] HandleRegister connId=%u\n", connId); std::fflush(stderr);
		auto parsed = ParseShardRegisterPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] Register: invalid payload (connId={})", connId);
			auto pkt = BuildShardRegisterErrorPacket(ShardRegisterErrorCode::InvalidPayload, requestId);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			std::fprintf(stderr, "[SREG] REGISTER_ERROR sent connId=%u\n", connId); std::fflush(stderr);
			return;
		}
		std::string name = parsed->name;
		std::string endpoint = parsed->endpoint;
		uint32_t current_load = parsed->current_load;
		auto id = m_registry->RegisterShard(std::move(parsed->name), std::move(parsed->endpoint), parsed->max_capacity, {});
		std::fprintf(stderr, "[SREG] RegisterShard id=%u (0=duplicate)\n", id ? *id : 0u); std::fflush(stderr);
		if (!id)
		{
			// Re-register (reconnect): find existing shard by name and return same id.
			auto list = m_registry->ListShards();
			for (const auto& s : list)
			{
				if (s.name == name)
				{
					m_registry->UpdateHeartbeat(s.shard_id, current_load);
					auto pkt = BuildShardRegisterOkPacket(s.shard_id, requestId);
					if (!pkt.empty() && m_server->Send(connId, pkt))
					{
						std::fprintf(stderr, "[SREG] REGISTER_OK sent connId=%u shard_id=%u\n", connId, s.shard_id); std::fflush(stderr);
						LOG_INFO(Core, "[ShardRegisterHandler] Re-register OK (connId={}, shard_id={})", connId, s.shard_id);
					}
					return;
				}
			}
			LOG_WARN(Core, "[ShardRegisterHandler] Register: duplicate name (connId={})", connId);
			auto pkt = BuildShardRegisterErrorPacket(ShardRegisterErrorCode::DuplicateName, requestId);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			std::fprintf(stderr, "[SREG] REGISTER_ERROR sent connId=%u\n", connId); std::fflush(stderr);
			return;
		}
		m_registry->UpdateHeartbeat(*id, current_load);
		auto pkt = BuildShardRegisterOkPacket(*id, requestId);
		if (!pkt.empty() && m_server->Send(connId, pkt))
		{
			std::fprintf(stderr, "[SREG] REGISTER_OK sent connId=%u shard_id=%u\n", connId, *id); std::fflush(stderr);
			LOG_INFO(Core, "[ShardRegisterHandler] Register OK (connId={}, shard_id={})", connId, *id);
		}
		else
		{
			std::fprintf(stderr, "[SREG] REGISTER_ERROR sent connId=%u\n", connId); std::fflush(stderr);
			LOG_ERROR(Core, "[ShardRegisterHandler] Register: send REGISTER_OK failed (connId={})", connId);
		}
	}

	void ShardRegisterHandler::HandleHeartbeat(uint32_t /*connId*/, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseShardHeartbeatPayload(payload, payloadSize);
		if (!parsed)
		{
			std::fprintf(stderr, "[SREG] HandleHeartbeat: parse failed\n"); std::fflush(stderr);
			return;
		}
		m_registry->UpdateHeartbeat(parsed->shard_id, parsed->current_load);
	}
}
