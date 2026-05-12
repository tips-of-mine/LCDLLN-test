#include "src/masterd/handlers/shard/ServerListHandler.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/masterd/shards/ServerRegistry.h"
#include "src/shared/network/ServerListPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

#include <vector>

namespace engine::server
{
	void ServerListHandler::SetServer(NetServer* server) { m_server = server; }
	void ServerListHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }
	void ServerListHandler::SetCharacterCountHook(std::function<uint32_t(uint32_t)> hook) { m_characterCountHook = std::move(hook); }
	void ServerListHandler::SetMasterSessionCountHook(std::function<uint32_t()> hook) { m_masterSessionCountHook = std::move(hook); }

	void ServerListHandler::SetServerRegistry(ServerRegistry* selfRegistry)
	{
		m_serverRegistry = selfRegistry;
	}

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
			// Compteur shards online/degraded : si UN SEUL est actif, on lui assigne
			// le total master-side (sessionCharMap.Count()) au lieu de s.current_load.
			// Meme logique que statusProvider dans main_linux.cpp pour /status JSON.
			// Sans ce fallback, current_load reste a 0 (heartbeat shard ne voit que
			// les TCP transitoires) -> client affiche 0/500 alors que API montre 1.
			uint32_t shardsOnline = 0;
			for (const auto& s : list)
			{
				if (s.state == engine::server::ShardState::Online
					|| s.state == engine::server::ShardState::Degraded)
					++shardsOnline;
			}
			const bool singleShardOnline = (shardsOnline == 1u);
			const uint32_t masterSessions = m_masterSessionCountHook ? m_masterSessionCountHook() : 0u;
			// Diagnostic explicite : permet de tracer en prod si la divergence
			// wire / HTTP /status vient du hook null, d'un sessionCharMap vide,
			// ou d'un branche `singleShardOnline=false` (multi-shard ou shard offline).
			LOG_INFO(Core,
				"[ServerListHandler] computing entries: list.size={} shardsOnline={} singleShardOnline={} hook_set={} masterSessions={}",
				list.size(), shardsOnline, singleShardOnline,
				m_masterSessionCountHook ? "yes" : "NO", masterSessions);
			entries.reserve(list.size());
			for (const auto& s : list)
			{
				const bool isOnline = (s.state == engine::server::ShardState::Online
					|| s.state == engine::server::ShardState::Degraded);
				engine::network::ServerListEntry e;
				e.shard_id = s.shard_id;
				e.status = static_cast<uint8_t>(s.state);
				e.current_load = (singleShardOnline && isOnline)
					? masterSessions
					: s.current_load;
				e.max_capacity = s.max_capacity;
				e.character_count = m_characterCountHook ? m_characterCountHook(s.shard_id) : 0u;
				e.endpoint = s.endpoint;
				LOG_INFO(Core,
					"[ServerListHandler]   shard_id={} state={} isOnline={} s.current_load={} -> e.current_load={} endpoint='{}'",
					s.shard_id, static_cast<uint32_t>(s.state), isOnline, s.current_load,
					e.current_load, s.endpoint);
				entries.push_back(e);
			}
		}
		// Ajouter l'entrée du master lui-même si ServerRegistry est disponible et enregistré.
		// Ne pas dupliquer un shard déjà listé : game_servers.id peut coïncider avec shard_id (ex. 1 et 1),
		// ce qui produisait deux lignes « monde #1 » (endpoint shard vs port master, capacités différentes).
		if (m_serverRegistry && m_serverRegistry->IsRegistered())
		{
			const uint32_t selfId = m_serverRegistry->GetServerId();
			bool shardIdAlreadyListed = false;
			for (const auto& e : entries)
			{
				if (e.shard_id == selfId)
				{
					shardIdAlreadyListed = true;
					break;
				}
			}
			if (!shardIdAlreadyListed)
			{
				engine::network::ServerListEntry self;
				self.shard_id        = selfId;
				self.status          = 1; // Online
				self.current_load    = 0;
				self.max_capacity    = m_serverRegistry->GetMaxPlayers();
				self.character_count = m_characterCountHook
					? m_characterCountHook(m_serverRegistry->GetServerId()) : 0u;
				self.endpoint        = m_serverRegistry->GetHost() + ":"
					                 + std::to_string(m_serverRegistry->GetPort());
				entries.push_back(std::move(self));
			}
			else
			{
				LOG_DEBUG(Core,
					"[ServerListHandler] Entrée master (server_id={}) omise : même shard_id déjà présent dans la liste des shards",
					selfId);
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
