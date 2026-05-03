#include "engine/server/CharacterCreateHandler.h"

#include "engine/core/Config.h"
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

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace engine::server
{
	void CharacterCreateHandler::SetServer(NetServer* server) { m_server = server; }
	void CharacterCreateHandler::SetSessionManager(SessionManager* sessions) { m_sessions = sessions; }
	void CharacterCreateHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void CharacterCreateHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }
	void CharacterCreateHandler::SetConfig(const engine::core::Config* config) { m_config = config; }

	bool CharacterCreateHandler::IsValidCharacterName(std::string_view name) const
	{
		if (name.size() < 3u || name.size() > 32u)
			return false;
		for (unsigned char c : name)
		{
			if (!std::isalnum(c) && c != '_')
				return false;
		}
		return true;
	}

	bool CharacterCreateHandler::IsForbiddenCharacterName(void* mysqlPtr, std::string_view name) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		char escapedName[128]{};
		mysql_real_escape_string(mysql, escapedName, std::string(name).c_str(), static_cast<unsigned long>(name.size()));
		std::string sql = "SELECT id FROM forbidden_character_names WHERE LOWER(name) = LOWER('";
		sql += escapedName;
		sql += "') LIMIT 1";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		const bool found = row != nullptr;
		engine::server::db::DbFreeResult(res);
		return found;
	}

	bool CharacterCreateHandler::CharacterNameExistsOnServer(void* mysqlPtr, std::string_view name, uint64_t serverId) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		char escapedName[128]{};
		mysql_real_escape_string(mysql, escapedName, std::string(name).c_str(), static_cast<unsigned long>(name.size()));
		std::string sql = "SELECT id FROM characters WHERE server_id = ";
		sql += std::to_string(serverId);
		sql += " AND LOWER(name) = LOWER('";
		sql += escapedName;
		sql += "') LIMIT 1";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		const bool found = row != nullptr;
		engine::server::db::DbFreeResult(res);
		return found;
	}

	uint64_t CharacterCreateHandler::ResolveDefaultServerId(void* mysqlPtr) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		const uint64_t configured = m_config ? static_cast<uint64_t>(std::max<int64_t>(0, m_config->GetInt("character_creation.default_server_id", 1))) : 1u;
		std::string sql = "SELECT id FROM servers WHERE id = " + std::to_string(configured) + " LIMIT 1";
		if (MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql))
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if (row && row[0])
			{
				uint64_t out = std::strtoull(row[0], nullptr, 10);
				engine::server::db::DbFreeResult(res);
				return out;
			}
			engine::server::db::DbFreeResult(res);
		}
		if (MYSQL_RES* res = engine::server::db::DbQuery(mysql, "SELECT id FROM servers ORDER BY id ASC LIMIT 1"))
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			uint64_t out = (row && row[0]) ? std::strtoull(row[0], nullptr, 10) : 0u;
			engine::server::db::DbFreeResult(res);
			return out;
		}
		return 0;
	}

	int CharacterCreateHandler::FindNextSlot(void* mysqlPtr, uint64_t accountId, uint64_t serverId) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		// Filtre 'deleted_at IS NULL' : sans cela, les persos soft-deletes via
		// CharacterDeleteHandler (qui pose deleted_at = NOW()) restent comptes
		// comme occupant un slot, et FindNextSlot retourne 1 au lieu de 0 apres
		// suppression d'un unique perso. Du coup le client affiche 'Slot 2' alors
		// qu'il devrait afficher 'Slot 1'.
		std::string sql = "SELECT slot FROM characters WHERE account_id = " + std::to_string(accountId)
			+ " AND server_id = " + std::to_string(serverId)
			+ " AND deleted_at IS NULL ORDER BY slot ASC";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
			return -1;
		bool used[5]{ false, false, false, false, false };
		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)))
		{
			if (row[0])
			{
				const unsigned long slot = std::strtoul(row[0], nullptr, 10);
				if (slot < 5u)
					used[slot] = true;
			}
		}
		engine::server::db::DbFreeResult(res);
		for (int i = 0; i < 5; ++i)
		{
			if (!used[i])
				return i;
		}
		return -1;
	}

	void CharacterCreateHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeCharacterCreateRequest || !m_server || !m_sessions || !m_connMap || !m_pool)
			return;

		auto parsed = ParseCharacterCreateRequestPayload(payload, payloadSize);
		if (!parsed || !IsValidCharacterName(parsed->name))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid character name", requestId, sessionIdHeader);
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

		const uint64_t serverId = ResolveDefaultServerId(mysql);
		if (serverId == 0)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "no target server configured", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (IsForbiddenCharacterName(mysql, parsed->name))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character name is forbidden", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (CharacterNameExistsOnServer(mysql, parsed->name, serverId))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character name already taken on this server", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const int slot = FindNextSlot(mysql, *accountId, serverId);
		if (slot < 0)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character slots full", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		char escapedName[128]{};
		mysql_real_escape_string(mysql, escapedName, parsed->name.c_str(), static_cast<unsigned long>(parsed->name.size()));

		// Phase 3.6 — Spawn par défaut lu depuis la config serveur (peut être surchargé
		// plus tard par le shard quand l'utilisateur se déconnecte / le serveur sauvegarde
		// la position courante du personnage). Défauts cohérents avec le défaut client
		// (config.json : client.world.default_spawn).
		const double spawnX     = m_config ? m_config->GetDouble("character_creation.default_spawn.x", 0.0) : 0.0;
		const double spawnY     = m_config ? m_config->GetDouble("character_creation.default_spawn.y", 100.0) : 100.0;
		const double spawnZ     = m_config ? m_config->GetDouble("character_creation.default_spawn.z", 0.0) : 0.0;
		const double spawnYaw   = m_config ? m_config->GetDouble("character_creation.default_spawn.yaw_deg", 0.0) : 0.0;
		const double spawnPitch = m_config ? m_config->GetDouble("character_creation.default_spawn.pitch_deg", -10.0) : -10.0;

		char spawnBuf[256]{};
		std::snprintf(spawnBuf, sizeof(spawnBuf), "%.6f, %.6f, %.6f, %.6f, %.6f",
			spawnX, spawnY, spawnZ, spawnYaw, spawnPitch);

		// Phase 3.8 — Persistance des identifiants chaîne race/classe choisis par
		// l'utilisateur (parsed->raceId / parsed->classId, ex. "humains" / "warrior").
		// Limite à 32 chars (taille de la colonne) ; plus court côté SQL après escape.
		auto truncate = [](const std::string& s) -> std::string {
			constexpr size_t kMax = 32u;
			return s.size() <= kMax ? s : s.substr(0, kMax);
		};
		const std::string raceIdTruncated  = truncate(parsed->raceId);
		const std::string classIdTruncated = truncate(parsed->classId);
		// PR-E : auto-assignation de la faction selon la race choisie.
		// Les factions sont race-lockees (cf. migration 0040 : Chevaliers de la
		// Lumiere / Justice / La Lune Noire / Empire de L'Hynn pour humains ;
		// Dzorak pour orcs ; Demons ; Chevaliers-Dragons ; etc.).
		// Pour les races avec faction unique on assigne directement. Pour les
		// races multi-factions (humains : 4 choix possibles) on laisse vide :
		// le joueur fera son choix au premier login via une UI dediee a venir.
		auto factionFromRace = [](std::string_view race) -> const char* {
			if (race == "orcs")               return "dzorak";
			if (race == "demons")             return "demons";
			if (race == "chevaliers_dragons") return "chevaliers_dragons";
			// humains, elfes, nains : faction(s) a choisir / pas encore definie -> vide.
			return "";
		};
		const std::string factionStr = factionFromRace(raceIdTruncated);
		char escapedRace[80]{};
		char escapedClass[80]{};
		char escapedFaction[80]{};
		mysql_real_escape_string(mysql, escapedRace,
			raceIdTruncated.c_str(), static_cast<unsigned long>(raceIdTruncated.size()));
		mysql_real_escape_string(mysql, escapedClass,
			classIdTruncated.c_str(), static_cast<unsigned long>(classIdTruncated.size()));
		mysql_real_escape_string(mysql, escapedFaction,
			factionStr.c_str(), static_cast<unsigned long>(factionStr.size()));

		std::string sql =
			"INSERT INTO characters (account_id, slot, name, server_id, race_id, class_id, level, appearance_json,"
			" spawn_x, spawn_y, spawn_z, spawn_yaw_deg, spawn_pitch_deg, race_str, class_str, faction_str) VALUES ("
			+ std::to_string(*accountId) + ", "
			+ std::to_string(slot) + ", '"
			+ escapedName + "', "
			+ std::to_string(serverId) + ", 0, 0, 1, '{}', "
			+ spawnBuf + ", '"
			+ escapedRace + "', '"
			+ escapedClass + "', '"
			+ escapedFaction + "')";
		if (!engine::server::db::DbExecute(mysql, sql))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character creation failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t characterId = static_cast<uint64_t>(mysql_insert_id(mysql));
		auto pkt = BuildCharacterCreateResponsePacket(1u, characterId, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		LOG_INFO(Auth, "[CharacterCreateHandler] Character created (account_id={}, character_id={}, server_id={}, slot={}, name={}, race='{}', class='{}', faction='{}', spawn=({}, {}, {}))",
			*accountId, characterId, serverId, slot, parsed->name, raceIdTruncated, classIdTruncated,
			factionStr.empty() ? "(unaligned)" : factionStr,
			spawnX, spawnY, spawnZ);
	}
}
