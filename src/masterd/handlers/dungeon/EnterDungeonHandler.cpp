#include "src/masterd/handlers/dungeon/EnterDungeonHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/network/DungeonPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"

#include <mysql.h>

#include <cstdio>
#include <string>

namespace engine::server
{
	void EnterDungeonHandler::SetServer(NetServer* server)                       { m_server = server; }
	void EnterDungeonHandler::SetSessionManager(SessionManager* sessions)        { m_sessions = sessions; }
	void EnterDungeonHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void EnterDungeonHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void EnterDungeonHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeEnterDungeonRequest || !m_server || !m_sessions || !m_connMap || !m_pool)
			return;

		auto parsed = ParseEnterDungeonRequestPayload(payload, payloadSize);
		if (!parsed || parsed->characterId == 0u || parsed->dungeonTemplateId.empty())
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid enter-dungeon payload",
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation session : connId → sessionId → accountId.
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (!connSessionId || *connSessionId == 0 || sessionIdHeader == 0 || *connSessionId != sessionIdHeader)
		{
			auto pkt = BuildEnterDungeonResponsePacket(false, 0u, "",
				kEnterDungeonErrorUnauthorized, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto accountId = m_sessions->GetAccountId(*connSessionId);
		if (!accountId)
		{
			auto pkt = BuildEnterDungeonResponsePacket(false, 0u, "",
				kEnterDungeonErrorUnauthorized, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable",
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Ownership : le personnage doit appartenir au compte de la session.
		char ownerQuery[256]{};
		std::snprintf(ownerQuery, sizeof(ownerQuery),
			"SELECT id FROM characters WHERE id = %llu AND account_id = %llu AND deleted_at IS NULL",
			static_cast<unsigned long long>(parsed->characterId),
			static_cast<unsigned long long>(*accountId));
		MYSQL_RES* ownerRes = engine::server::db::DbQuery(mysql, ownerQuery);
		bool owned = false;
		if (ownerRes)
		{
			MYSQL_ROW row = mysql_fetch_row(ownerRes);
			owned = (row && row[0]);
			engine::server::db::DbFreeResult(ownerRes);
		}
		if (!owned)
		{
			LOG_WARN(Net, "[EnterDungeonHandler] character not owned (account_id={}, character_id={})",
				*accountId, parsed->characterId);
			auto pkt = BuildEnterDungeonResponsePacket(false, 0u, "",
				kEnterDungeonErrorUnauthorized, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// INSERT de l'instance de donjon. Le dungeon_template_id est
		// échappé (valeur client). shard_endpoint reste vide (résolution
		// multi-instance = follow-up).
		std::string escapedTemplate;
		escapedTemplate.resize(parsed->dungeonTemplateId.size() * 2u + 1u);
		const unsigned long escLen = mysql_real_escape_string(mysql, escapedTemplate.data(),
			parsed->dungeonTemplateId.c_str(),
			static_cast<unsigned long>(parsed->dungeonTemplateId.size()));
		escapedTemplate.resize(escLen);

		std::string insertQuery =
			"INSERT INTO dungeon_instances "
			"(dungeon_template_id, owner_character_id, difficulty, shard_endpoint) VALUES ('";
		insertQuery += escapedTemplate;
		insertQuery += "', ";
		insertQuery += std::to_string(static_cast<unsigned long long>(parsed->characterId));
		insertQuery += ", ";
		insertQuery += std::to_string(static_cast<unsigned>(parsed->difficulty));
		insertQuery += ", '')";

		if (!engine::server::db::DbExecute(mysql, insertQuery))
		{
			LOG_ERROR(Net, "[EnterDungeonHandler] INSERT dungeon_instances failed (character_id={}, template='{}')",
				parsed->characterId, parsed->dungeonTemplateId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "dungeon instance create failed",
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t instanceId = static_cast<uint64_t>(mysql_insert_id(mysql));
		LOG_INFO(Net, "[EnterDungeonHandler] dungeon instance created (id={}, template='{}', owner={}, difficulty={})",
			instanceId, parsed->dungeonTemplateId, parsed->characterId, parsed->difficulty);

		// shardEndpoint vide : le client reçoit l'instanceId, le routage
		// shard de donjon est un follow-up post-Phase 11.
		auto pkt = BuildEnterDungeonResponsePacket(true, instanceId, "",
			kEnterDungeonErrorNone, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
