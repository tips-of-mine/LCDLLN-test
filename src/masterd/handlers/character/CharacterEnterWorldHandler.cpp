#include "src/masterd/handlers/character/CharacterEnterWorldHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/masterd/account/AccountRole.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace engine::server
{
	void CharacterEnterWorldHandler::SetServer(NetServer* server)                       { m_server = server; }
	void CharacterEnterWorldHandler::SetSessionManager(SessionManager* sessions)        { m_sessions = sessions; }
	void CharacterEnterWorldHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterEnterWorldHandler::SetSessionCharacterMap(SessionCharacterMap* charMap) { m_charMap = charMap; }
	void CharacterEnterWorldHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }
	void CharacterEnterWorldHandler::SetShardRegistry(ShardRegistry* registry) { m_shardRegistry = registry; }

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

		// Vérifie ownership : SELECT name, server_id FROM characters WHERE id=? AND account_id=? AND deleted_at IS NULL.
		// On compare ensuite le name DB au name client byte-pour-byte (rejette toute imposture
		// comme un sender qui claim un autre nom que celui en DB). TA.3 : server_id sert au
		// lookup du shard cible pour le push d'admission.
		char queryBuf[256]{};
		std::snprintf(queryBuf, sizeof(queryBuf),
			"SELECT name, server_id FROM characters WHERE id = %llu AND account_id = %llu AND deleted_at IS NULL",
			static_cast<unsigned long long>(parsed->characterId),
			static_cast<unsigned long long>(*accountId));

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, queryBuf);
		std::string dbName;
		uint32_t dbServerId = 0;
		bool found = false;
		if (res)
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if (row && row[0])
			{
				dbName = row[0];
				if (row[1])
					dbServerId = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
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

		// Rôle du compte : alimente la ventilation par rôle de l'API /status
		// (players_by_role). Une requête séparée et tolérante aux erreurs — un rôle
		// indéterminé retombe sur Player (sentinel sûr de ParseRole).
		AccountRole accountRole = AccountRole::Player;
		{
			char roleQuery[160]{};
			std::snprintf(roleQuery, sizeof(roleQuery),
				"SELECT role FROM accounts WHERE id = %llu",
				static_cast<unsigned long long>(*accountId));
			MYSQL_RES* roleRes = engine::server::db::DbQuery(mysql, roleQuery);
			if (roleRes)
			{
				MYSQL_ROW roleRow = mysql_fetch_row(roleRes);
				if (roleRow && roleRow[0])
					accountRole = engine::server::ParseRole(roleRow[0]);
				engine::server::db::DbFreeResult(roleRes);
			}
		}

		// Tout est validé : on enregistre le mapping pour le chat.
		const std::string normalized = SessionCharacterMap::Normalize(parsed->characterName);
		m_charMap->Set(connId, parsed->characterId, parsed->characterName, normalized, accountRole);
		LOG_INFO(Net, "[CharacterEnterWorldHandler] registered (account_id={}, character_id={}, name='{}')",
			*accountId, parsed->characterId, parsed->characterName);

		// TA.3 — push d'admission au shard. Sans ça, le client envoie son Hello UDP avec
		// `clientNonce=character_id`, mais le shard l'a admis au handshake TCP avec
		// `character_id=0` (le ticket avait été demandé AVANT le choix de perso), donc rejet.
		// Le push réutilise la connexion TCP long-vivante établie par ShardToMasterClient.
		if (m_shardRegistry != nullptr && dbServerId != 0u)
		{
			auto shardConnId = m_shardRegistry->GetShardConnection(dbServerId);
			if (shardConnId)
			{
				auto admitPkt = engine::network::BuildAdmitCharacterPacket(*accountId, parsed->characterId);
				if (!admitPkt.empty() && m_server->Send(*shardConnId, admitPkt))
				{
					LOG_INFO(Net, "[CharacterEnterWorldHandler] admit push sent (shard_id={}, shardConnId={}, account_id={}, character_id={})",
						dbServerId, *shardConnId, *accountId, parsed->characterId);
				}
				else
				{
					LOG_WARN(Net, "[CharacterEnterWorldHandler] admit push send FAILED (shard_id={}, shardConnId={}, account_id={}, character_id={})",
						dbServerId, *shardConnId, *accountId, parsed->characterId);
				}
			}
			else
			{
				LOG_WARN(Net, "[CharacterEnterWorldHandler] admit push skipped: no shard connection (shard_id={}, account_id={}, character_id={})",
					dbServerId, *accountId, parsed->characterId);
			}
		}
		else if (m_shardRegistry == nullptr)
		{
			LOG_WARN(Net, "[CharacterEnterWorldHandler] admit push skipped: ShardRegistry not configured (account_id={}, character_id={})",
				*accountId, parsed->characterId);
		}

		auto pkt = BuildCharacterEnterWorldResponsePacket(1u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
