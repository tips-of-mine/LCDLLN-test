#include "src/masterd/handlers/character/CharacterEnterWorldHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <string>

namespace engine::server
{
	void CharacterEnterWorldHandler::SetServer(NetServer* server)                       { m_server = server; }
	void CharacterEnterWorldHandler::SetSessionManager(SessionManager* sessions)        { m_sessions = sessions; }
	void CharacterEnterWorldHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterEnterWorldHandler::SetSessionCharacterMap(SessionCharacterMap* charMap) { m_charMap = charMap; }
	void CharacterEnterWorldHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void CharacterEnterWorldHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeCharacterEnterWorldRequest || !m_server || !m_sessions || !m_connMap || !m_charMap || !m_pool)
			return;

		auto parsed = ParseCharacterEnterWorldRequestPayload(payload, payloadSize);
		if (!parsed || parsed->characterId == 0u || parsed->characterName.empty())
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid enter-world payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Bornage long de nom (idem CharacterCreate côté DB).
		if (parsed->characterName.size() > 32u)
		{
			parsed->characterName.resize(32u);
		}

		auto connSessionId = m_connMap->GetSessionId(connId);
		if (!connSessionId || *connSessionId == 0 || sessionIdHeader == 0 || *connSessionId != sessionIdHeader)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "session required", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto accountId = m_sessions->GetAccountId(*connSessionId);
		if (!accountId)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "account missing", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Vérifie ownership : SELECT name FROM characters WHERE id=? AND account_id=? AND deleted_at IS NULL.
		// On compare ensuite le name DB au name client byte-pour-byte (rejette toute imposture
		// comme un sender qui claim un autre nom que celui en DB).
		char queryBuf[256]{};
		std::snprintf(queryBuf, sizeof(queryBuf),
			"SELECT name FROM characters WHERE id = %llu AND account_id = %llu AND deleted_at IS NULL",
			static_cast<unsigned long long>(parsed->characterId),
			static_cast<unsigned long long>(*accountId));

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, queryBuf);
		std::string dbName;
		bool found = false;
		if (res)
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if (row && row[0])
			{
				dbName = row[0];
				found = true;
			}
			engine::server::db::DbFreeResult(res);
		}

		if (!found)
		{
			LOG_WARN(Auth, "[CharacterEnterWorldHandler] character not owned (account_id={}, character_id={})",
				*accountId, parsed->characterId);
			auto pkt = BuildCharacterEnterWorldResponsePacket(0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (dbName != parsed->characterName)
		{
			LOG_WARN(Auth, "[CharacterEnterWorldHandler] name mismatch (db='{}' client='{}', character_id={})",
				dbName, parsed->characterName, parsed->characterId);
			auto pkt = BuildCharacterEnterWorldResponsePacket(0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Tout est validé : on enregistre le mapping pour le chat.
		const std::string normalized = SessionCharacterMap::Normalize(parsed->characterName);
		m_charMap->Set(connId, parsed->characterId, parsed->characterName, normalized);
		LOG_INFO(Net, "[CharacterEnterWorldHandler] registered (account_id={}, character_id={}, name='{}')",
			*accountId, parsed->characterId, parsed->characterName);

		auto pkt = BuildCharacterEnterWorldResponsePacket(1u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
