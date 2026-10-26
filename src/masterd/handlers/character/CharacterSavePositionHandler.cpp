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

		// ETAPE2.1 — la base n'a qu'un écrivain par personnage : le shard propriétaire
		// (ServerApp::SaveCharacterToDb écrit spawn_*, niveau et tout l'état). Le master
		// n'écrit plus la position envoyée par le client ; il acquitte la requête pour
		// que les clients existants ne restent pas en attente d'une réponse.
		LOG_DEBUG(Auth, "[CharacterSavePositionHandler] ignoré : la position est écrite par le shard (account_id={}, character_id={})",
			*accountId, parsed->characterId);
		auto pkt = BuildCharacterSavePositionResponsePacket(1u, requestId, sessionIdHeader);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Auth, "[CharacterSavePositionHandler] Send response failed (connId={}, account_id={}, character_id={})",
				connId, *accountId, parsed->characterId);
		}
	}
}
