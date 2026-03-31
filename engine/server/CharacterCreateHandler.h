#pragma once

#include <cstdint>
#include <string>

namespace engine::core
{
	class Config;
}

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;

	class CharacterCreateHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);
		void SetConfig(const engine::core::Config* config);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		bool IsValidCharacterName(std::string_view name) const;
		bool IsForbiddenCharacterName(void* mysqlPtr, std::string_view name) const;
		bool CharacterNameExistsOnServer(void* mysqlPtr, std::string_view name, uint64_t serverId) const;
		uint64_t ResolveDefaultServerId(void* mysqlPtr) const;
		int FindNextSlot(void* mysqlPtr, uint64_t accountId, uint64_t serverId) const;

		NetServer* m_server = nullptr;
		SessionManager* m_sessions = nullptr;
		ConnectionSessionMap* m_connMap = nullptr;
		engine::server::db::ConnectionPool* m_pool = nullptr;
		const engine::core::Config* m_config = nullptr;
	};
}
