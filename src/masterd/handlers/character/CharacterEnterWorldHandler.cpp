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
#include "src/masterd/anniversary/AnniversaryService.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <chrono>
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
	void CharacterEnterWorldHandler::SetAnniversaryService(AnniversaryService* service) { m_anniversary = service; }

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
		auto* stmtCache = guard.cache();
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Vérifie ownership : SELECT name, server_id, gender FROM characters WHERE id=? AND account_id=?
		// AND deleted_at IS NULL. On compare ensuite le name DB au name client byte-pour-byte
		// (rejette toute imposture comme un sender qui claim un autre nom que celui en DB).
		// TA.3 : server_id sert au lookup du shard cible pour le push d'admission.
		// TD.6 : gender (migration 0067, "male"/"female") propagé au shard via AdmitCharacter.
		// N1-B : prepared statement (bind characterId, accountId).
		std::string dbName;
		std::string dbGender;
		uint32_t dbServerId = 0;
		bool found = false;
		if (stmtCache)
		{
			auto* stmt = stmtCache->Acquire(mysql,
				"SELECT name, server_id, gender FROM characters WHERE id = ? AND account_id = ? AND deleted_at IS NULL");
			if (stmt
				&& stmt->Bind(0, parsed->characterId)
				&& stmt->Bind(1, *accountId)
				&& stmt->Execute()
				&& stmt->FetchRow())
			{
				dbName = stmt->GetString(0);
				dbServerId = static_cast<uint32_t>(stmt->GetUInt64(1));
				dbGender = stmt->GetString(2);
				found = !dbName.empty();
			}
		}
		else
		{
			LOG_WARN(Net, "[CharacterEnterWorldHandler] ownership query : cache absent");
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
		// N1-B : prepared statement (bind accountId).
		AccountRole accountRole = AccountRole::Player;
		if (stmtCache)
		{
			auto* roleStmt = stmtCache->Acquire(mysql, "SELECT role FROM accounts WHERE id = ?");
			if (roleStmt
				&& roleStmt->Bind(0, *accountId)
				&& roleStmt->Execute()
				&& roleStmt->FetchRow())
			{
				const std::string roleStr = roleStmt->GetString(0);
				if (!roleStr.empty())
					accountRole = engine::server::ParseRole(roleStr.c_str());
			}
		}

		// Tout est validé : on enregistre le mapping pour le chat.
		const std::string normalized = SessionCharacterMap::Normalize(parsed->characterName);
		m_charMap->Set(connId, *accountId, parsed->characterId, parsed->characterName, normalized, accountRole);
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
				// TD.5 — on embarque le nom du personnage dans le push d'admission pour
				// permettre au shard (notamment en mode no-DB) de l'utiliser dans la
				// SnapshotEntity (plaque de nom des avatars distants).
				// TD.6 — on embarque aussi le genre (cf. migration 0067) pour permettre au
				// client de sélectionner le bon mesh skinné (Male_Ranger vs Female_Ranger)
				// pour les avatars distants.
				auto admitPkt = engine::network::BuildAdmitCharacterPacket(*accountId, parsed->characterId,
					parsed->characterName, dbGender);
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

		// Anniversaires (spec 2026-07-18) — après l'admission validée :
		// paliers fidélité (rattrapage) + jour J de naissance (exploit +
		// courrier cadeau). Les déblocages sont annoncés au client entrant
		// par un ChatRelay « système » (même pattern que les notices du
		// ChatRelayHandler). Le service est idempotent : un re-EnterWorld
		// (changement de perso) ne double aucun octroi.
		if (m_anniversary != nullptr)
		{
			const auto anniv = m_anniversary->OnEnterWorld(*accountId, engine::anniversary::TodayUtc());
			const uint64_t nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			for (const std::string& title : anniv.unlockedTitles)
			{
				auto notice = BuildChatRelayPacket(nowMs,
					static_cast<uint8_t>(engine::net::ChatChannel::Server),
					"system", "Exploit débloqué : " + title, sessionIdHeader);
				if (!notice.empty())
					m_server->Send(connId, notice);
			}
			if (anniv.birthdayGiftMailed)
			{
				auto notice = BuildChatRelayPacket(nowMs,
					static_cast<uint8_t>(engine::net::ChatChannel::Server),
					"system",
					"Joyeux anniversaire ! Un cadeau vous attend dans votre courrier.",
					sessionIdHeader);
				if (!notice.empty())
					m_server->Send(connId, notice);
			}
		}
	}
}
