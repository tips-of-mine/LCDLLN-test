#include "src/masterd/handlers/character/CharacterSavePositionHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/db/SqlPreparedStatement.h"

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
		auto* stmtCache = guard.cache();
		if (!mysql || !stmtCache)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// UPDATE gating par account_id : impossible de saver pour le perso d'un autre compte.
		// Aussi gate sur deleted_at IS NULL — on ne sauve pas la position d'un perso supprimé.
		// N1-C : prepared statement (5 floats spawn + 2 ids). Bind double car
		// l'API SqlPreparedStatement n'a pas de surcharge float (les float sont
		// stockés en DOUBLE en interne MySQL si la colonne est FLOAT, la conversion
		// est correcte).
		auto* stmt = stmtCache->Acquire(mysql,
			"UPDATE characters SET spawn_x = ?, spawn_y = ?, spawn_z = ?, "
			"spawn_yaw_deg = ?, spawn_pitch_deg = ? "
			"WHERE id = ? AND account_id = ? AND deleted_at IS NULL");
		if (!stmt
			|| !stmt->Bind(0, static_cast<double>(parsed->x))
			|| !stmt->Bind(1, static_cast<double>(parsed->y))
			|| !stmt->Bind(2, static_cast<double>(parsed->z))
			|| !stmt->Bind(3, static_cast<double>(parsed->yawDeg))
			|| !stmt->Bind(4, static_cast<double>(parsed->pitchDeg))
			|| !stmt->Bind(5, parsed->characterId)
			|| !stmt->Bind(6, *accountId)
			|| !stmt->Execute())
		{
			LOG_ERROR(Auth, "[CharacterSavePositionHandler] UPDATE failed (account_id={}, character_id={})",
				*accountId, parsed->characterId);
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "save position failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t affected = stmt->AffectedRows();
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
