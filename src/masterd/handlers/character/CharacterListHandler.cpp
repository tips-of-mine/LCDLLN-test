#include "src/masterd/handlers/character/CharacterListHandler.h"

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

#include <cstdlib>

namespace engine::server
{
	void CharacterListHandler::SetServer(NetServer* server) { m_server = server; }
	void CharacterListHandler::SetSessionManager(SessionManager* sessions) { m_sessions = sessions; }
	void CharacterListHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterListHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void CharacterListHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeCharacterListRequest || !m_server || !m_sessions || !m_connMap || !m_pool)
			return;

		auto parsed = ParseCharacterListRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid character list request", requestId, sessionIdHeader);
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
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		if (!stmtCache)
		{
			LOG_ERROR(Auth, "[CharacterListHandler] cache de prepared statements absent");
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "prepared statement cache unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// LEFT JOIN character_stats so characters who never logged in still appear (last_seen NULL → 0).
		// Phase 3.6 — spawn position lue depuis characters.spawn_{x,y,z,yaw_deg,pitch_deg}.
		// Phase 3.8 — race_str / class_str (chaînes ; cf. game/data/races/{races,classes}.json).
		// N1-B : prepared statement (bind accountId, serverId). Les colonnes float
		// (spawn_*) sont lues via GetString() + strtof() — l'API SqlPreparedStatement
		// lit tous les résultats en string (MYSQL_TYPE_STRING) et le parsing client
		// préserve la précision.
		auto* stmt = stmtCache->Acquire(mysql,
			"SELECT c.id, c.slot, c.name, c.race_id, c.class_id, c.level, c.force_rename, "
			"COALESCE(UNIX_TIMESTAMP(s.last_seen), 0) AS last_seen_unix, "
			"COALESCE(s.total_play_seconds, 0) AS total_play, "
			"c.spawn_x, c.spawn_y, c.spawn_z, c.spawn_yaw_deg, c.spawn_pitch_deg, "
			"c.race_str, c.class_str, c.gender, c.skin_color_idx "
			"FROM characters c "
			"LEFT JOIN character_stats s ON s.character_id = c.id AND s.server_id = c.server_id "
			"WHERE c.account_id = ? AND c.server_id = ? AND c.deleted_at IS NULL "
			"ORDER BY c.slot ASC");
		if (!stmt
			|| !stmt->Bind(0, *accountId)
			|| !stmt->Bind(1, static_cast<uint64_t>(parsed->serverId))
			|| !stmt->Execute())
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "character list query failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		std::vector<CharacterListEntry> entries;
		while (stmt->FetchRow())
		{
			CharacterListEntry e;
			e.character_id    = stmt->GetUInt64(0);
			e.slot            = static_cast<uint8_t>(stmt->GetUInt64(1));
			e.name            = stmt->GetString(2);
			e.race_id         = static_cast<uint32_t>(stmt->GetUInt64(3));
			e.class_id        = static_cast<uint16_t>(stmt->GetUInt64(4));
			e.level           = static_cast<uint16_t>(stmt->GetUInt64(5));
			e.force_rename    = static_cast<uint8_t>(stmt->GetUInt64(6) != 0u ? 1u : 0u);
			e.last_seen_unix  = stmt->GetUInt64(7);
			e.total_play_secs = stmt->GetUInt64(8);
			// Phase 3.6 — spawn (5 colonnes ajoutées par migration 0032). Float via GetString + strtof.
			e.spawn_x         = std::strtof(stmt->GetString(9).c_str(), nullptr);
			e.spawn_y         = std::strtof(stmt->GetString(10).c_str(), nullptr);
			e.spawn_z         = std::strtof(stmt->GetString(11).c_str(), nullptr);
			e.spawn_yaw_deg   = std::strtof(stmt->GetString(12).c_str(), nullptr);
			e.spawn_pitch_deg = std::strtof(stmt->GetString(13).c_str(), nullptr);
			// Phase 3.8 — race / class strings (migration 0033).
			e.race_str  = stmt->GetString(14);
			e.class_str = stmt->GetString(15);
			// #1 serveur — genre (migration 0067). Defaut 'male' si vide/NULL.
			e.gender = stmt->GetString(16);
			if (e.gender.empty()) e.gender = "male";
			// Teinte de peau (migration 0068). Defaut 0 (claire) si vide/NULL.
			e.skin_color_idx = static_cast<uint8_t>(stmt->GetUInt64(17));
			entries.push_back(std::move(e));
		}

		auto pkt = BuildCharacterListResponsePacket(1u, entries, requestId, sessionIdHeader);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Auth, "[CharacterListHandler] Send CHARACTER_LIST_RESPONSE failed (connId={}, account_id={}, server_id={})",
				connId, *accountId, parsed->serverId);
			return;
		}
		LOG_INFO(Auth, "[CharacterListHandler] CHARACTER_LIST_RESPONSE sent (connId={}, account_id={}, server_id={}, count={})",
			connId, *accountId, parsed->serverId, entries.size());
	}
}
