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
#include "src/shared/Character/CustomizationJson.h"

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
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// LEFT JOIN character_stats so characters who never logged in still appear (last_seen NULL → 0).
		// Phase 3.6 — spawn position lue depuis characters.spawn_{x,y,z,yaw_deg,pitch_deg}.
		// Phase 3.8 — race_str / class_str (chaînes ; cf. game/data/races/{races,classes}.json).
		std::string sql =
			"SELECT c.id, c.slot, c.name, c.race_id, c.class_id, c.level, c.force_rename, "
			"COALESCE(UNIX_TIMESTAMP(s.last_seen), 0) AS last_seen_unix, "
			"COALESCE(s.total_play_seconds, 0) AS total_play, "
			"c.spawn_x, c.spawn_y, c.spawn_z, c.spawn_yaw_deg, c.spawn_pitch_deg, "
			"c.race_str, c.class_str, c.appearance_json "
			"FROM characters c "
			"LEFT JOIN character_stats s ON s.character_id = c.id AND s.server_id = c.server_id "
			"WHERE c.account_id = " + std::to_string(*accountId)
			+ " AND c.server_id = " + std::to_string(parsed->serverId)
			+ " AND c.deleted_at IS NULL "
			"ORDER BY c.slot ASC";

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "character list query failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		std::vector<CharacterListEntry> entries;
		MYSQL_ROW row = nullptr;
		while ((row = mysql_fetch_row(res)) != nullptr)
		{
			CharacterListEntry e;
			if (row[0]) e.character_id    = std::strtoull(row[0], nullptr, 10);
			if (row[1]) e.slot            = static_cast<uint8_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) e.name            = row[2];
			if (row[3]) e.race_id         = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4]) e.class_id        = static_cast<uint16_t>(std::strtoul(row[4], nullptr, 10));
			if (row[5]) e.level           = static_cast<uint16_t>(std::strtoul(row[5], nullptr, 10));
			if (row[6]) e.force_rename    = static_cast<uint8_t>(std::strtoul(row[6], nullptr, 10) != 0u ? 1u : 0u);
			if (row[7]) e.last_seen_unix  = std::strtoull(row[7], nullptr, 10);
			if (row[8]) e.total_play_secs = std::strtoull(row[8], nullptr, 10);
			// Phase 3.6 — spawn (5 colonnes ajoutées par migration 0032).
			if (row[9])  e.spawn_x         = std::strtof(row[9], nullptr);
			if (row[10]) e.spawn_y         = std::strtof(row[10], nullptr);
			if (row[11]) e.spawn_z         = std::strtof(row[11], nullptr);
			if (row[12]) e.spawn_yaw_deg   = std::strtof(row[12], nullptr);
			if (row[13]) e.spawn_pitch_deg = std::strtof(row[13], nullptr);
			// Phase 3.8 — race / class strings (migration 0033).
			if (row[14]) e.race_str  = row[14];
			if (row[15]) e.class_str = row[15];
			// Slice 1 — appearance customization (NULL/'{}' → défauts).
			if (row[16]) e.customization = engine::character::CustomizationFromJson(row[16]);
			entries.push_back(std::move(e));
		}
		engine::server::db::DbFreeResult(res);

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
