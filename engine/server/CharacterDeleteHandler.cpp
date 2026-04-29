#include "engine/server/CharacterDeleteHandler.h"

#include "engine/core/Log.h"
#include "engine/network/CharacterPayloads.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/NetServer.h"
#include "engine/server/SessionManager.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"

#include <mysql.h>

namespace engine::server
{
	void CharacterDeleteHandler::SetServer(NetServer* server) { m_server = server; }
	void CharacterDeleteHandler::SetSessionManager(SessionManager* sessions) { m_sessions = sessions; }
	void CharacterDeleteHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterDeleteHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void CharacterDeleteHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeCharacterDeleteRequest || !m_server || !m_sessions || !m_connMap || !m_pool)
			return;

		auto parsed = ParseCharacterDeleteRequestPayload(payload, payloadSize);
		if (!parsed || parsed->characterId == 0u)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid character_id", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
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

		// Soft delete : set deleted_at = NOW() conditionnellement à l'appartenance.
		// Si le perso n'existe pas OU n'appartient pas au compte OU est déjà supprimé,
		// l'UPDATE renvoie 0 affected rows — on répond NOT_FOUND sans révéler l'existence.
		std::string sql =
			"UPDATE characters SET deleted_at = NOW() "
			"WHERE id = " + std::to_string(parsed->characterId)
			+ " AND account_id = " + std::to_string(*accountId)
			+ " AND deleted_at IS NULL";
		if (!engine::server::db::DbExecute(mysql, sql))
		{
			LOG_ERROR(Auth, "[CharacterDeleteHandler] UPDATE failed (account_id={}, character_id={})",
				*accountId, parsed->characterId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "delete failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const my_ulonglong affected = mysql_affected_rows(mysql);
		if (affected == 0u)
		{
			LOG_WARN(Auth, "[CharacterDeleteHandler] no row updated (account_id={}, character_id={}) — not owned, missing or already deleted",
				*accountId, parsed->characterId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character not found or not owned", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		auto pkt = BuildCharacterDeleteResponsePacket(1u, requestId, sessionIdHeader);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Auth, "[CharacterDeleteHandler] Send response failed (connId={}, account_id={}, character_id={})",
				connId, *accountId, parsed->characterId);
			return;
		}
		LOG_INFO(Auth, "[CharacterDeleteHandler] Character soft-deleted (account_id={}, character_id={})",
			*accountId, parsed->characterId);
	}
}
