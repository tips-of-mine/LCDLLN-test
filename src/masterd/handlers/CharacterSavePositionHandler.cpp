#include "src/masterd/handlers/CharacterSavePositionHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cmath>
#include <cstdio>

namespace engine::server
{
	void CharacterSavePositionHandler::SetServer(NetServer* server) { m_server = server; }
	void CharacterSavePositionHandler::SetSessionManager(SessionManager* sessions) { m_sessions = sessions; }
	void CharacterSavePositionHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterSavePositionHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void CharacterSavePositionHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeCharacterSavePositionRequest || !m_server || !m_sessions || !m_connMap || !m_pool)
			return;

		auto parsed = ParseCharacterSavePositionRequestPayload(payload, payloadSize);
		if (!parsed || parsed->characterId == 0u)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid save position payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Garde-fou : rejeter les NaN / Inf qui pourraient corrompre la DB.
		auto isFinite = [](float f) { return std::isfinite(f); };
		if (!isFinite(parsed->x) || !isFinite(parsed->y) || !isFinite(parsed->z)
			|| !isFinite(parsed->yawDeg) || !isFinite(parsed->pitchDeg))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "non-finite position values", requestId, sessionIdHeader);
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

		// UPDATE gating par account_id : impossible de saver pour le perso d'un autre compte.
		// Aussi gate sur deleted_at IS NULL — on ne sauve pas la position d'un perso supprimé.
		char buf[512]{};
		std::snprintf(buf, sizeof(buf),
			"UPDATE characters SET spawn_x = %.6f, spawn_y = %.6f, spawn_z = %.6f, "
			"spawn_yaw_deg = %.6f, spawn_pitch_deg = %.6f "
			"WHERE id = %llu AND account_id = %llu AND deleted_at IS NULL",
			static_cast<double>(parsed->x), static_cast<double>(parsed->y), static_cast<double>(parsed->z),
			static_cast<double>(parsed->yawDeg), static_cast<double>(parsed->pitchDeg),
			static_cast<unsigned long long>(parsed->characterId),
			static_cast<unsigned long long>(*accountId));

		if (!engine::server::db::DbExecute(mysql, buf))
		{
			LOG_ERROR(Auth, "[CharacterSavePositionHandler] UPDATE failed (account_id={}, character_id={})",
				*accountId, parsed->characterId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "save position failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const my_ulonglong affected = mysql_affected_rows(mysql);
		if (affected == 0u)
		{
			LOG_WARN(Auth, "[CharacterSavePositionHandler] no row updated (account_id={}, character_id={}) — not owned, missing or deleted",
				*accountId, parsed->characterId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character not found or not owned", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		auto pkt = BuildCharacterSavePositionResponsePacket(1u, requestId, sessionIdHeader);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Auth, "[CharacterSavePositionHandler] Send response failed (connId={}, account_id={}, character_id={})",
				connId, *accountId, parsed->characterId);
			return;
		}
		LOG_INFO(Auth, "[CharacterSavePositionHandler] Position saved (account_id={}, character_id={}, pos=({:.2f}, {:.2f}, {:.2f}))",
			*accountId, parsed->characterId, parsed->x, parsed->y, parsed->z);
	}
}
