#include "src/masterd/handlers/character/CharacterCreateHandler.h"

#include "src/shared/core/Config.h"
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

	bool CharacterCreateHandler::IsForbiddenCharacterName(void* mysqlPtr,
		engine::server::db::SqlPreparedStatementCache* cache, std::string_view name) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		if (!cache)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] IsForbiddenCharacterName : cache de prepared statements absent");
			return false;
		}
		auto* stmt = cache->Acquire(mysql,
			"SELECT id FROM forbidden_character_names WHERE LOWER(name) = LOWER(?) LIMIT 1");
		if (!stmt)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] IsForbiddenCharacterName : Acquire prepared stmt a échoué");
			return false;
		}
		if (!stmt->Bind(0, name)) return false;
		if (!stmt->Execute()) return false;
		return stmt->FetchRow();
	}

	bool CharacterCreateHandler::CharacterNameExistsOnServer(void* mysqlPtr,
		engine::server::db::SqlPreparedStatementCache* cache, std::string_view name, uint64_t serverId) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		if (!cache)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] CharacterNameExistsOnServer : cache absent");
			return false;
		}
		// Filtre 'deleted_at IS NULL' : sans cela, un nom de perso
		// soft-delete reste reserve a vie et empeche l'utilisateur de
		// recreer un perso avec le meme nom (meme s'il l'a lui-meme
		// supprime). MVP : on libere le nom des la suppression. Une
		// future evolution pourra ajouter un delai de carence anti-squat.
		auto* stmt = cache->Acquire(mysql,
			"SELECT id FROM characters WHERE server_id = ? AND LOWER(name) = LOWER(?) AND deleted_at IS NULL LIMIT 1");
		if (!stmt)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] CharacterNameExistsOnServer : Acquire prepared stmt a échoué");
			return false;
		}
		if (!stmt->Bind(0, serverId)) return false;
		if (!stmt->Bind(1, name)) return false;
		if (!stmt->Execute()) return false;
		return stmt->FetchRow();
	}

	uint64_t CharacterCreateHandler::ResolveDefaultServerId(void* mysqlPtr,
		engine::server::db::SqlPreparedStatementCache* cache) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		const uint64_t configured = m_config ? static_cast<uint64_t>(std::max<int64_t>(0, m_config->GetInt("character_creation.default_server_id", 1))) : 1u;
		if (!cache)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] ResolveDefaultServerId : cache absent");
			return 0;
		}
		// Premier essai : ID configuré explicitement.
		{
			auto* stmt = cache->Acquire(mysql, "SELECT id FROM servers WHERE id = ? LIMIT 1");
			if (stmt && stmt->Bind(0, configured) && stmt->Execute() && stmt->FetchRow())
				return stmt->GetUInt64(0);
		}
		// Fallback : premier serveur de la table par ordre d'ID (pas de paramètre).
		{
			auto* stmt = cache->Acquire(mysql, "SELECT id FROM servers ORDER BY id ASC LIMIT 1");
			if (stmt && stmt->Execute() && stmt->FetchRow())
				return stmt->GetUInt64(0);
		}
		return 0;
	}

	int CharacterCreateHandler::FindNextSlot(void* mysqlPtr,
		engine::server::db::SqlPreparedStatementCache* cache,
		uint64_t accountId, uint64_t serverId) const
	{
		MYSQL* mysql = reinterpret_cast<MYSQL*>(mysqlPtr);
		if (!cache)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] FindNextSlot : cache absent");
			return -1;
		}
		// Filtre 'deleted_at IS NULL' : sans cela, les persos soft-deletes via
		// CharacterDeleteHandler (qui pose deleted_at = NOW()) restent comptes
		// comme occupant un slot, et FindNextSlot retourne 1 au lieu de 0 apres
		// suppression d'un unique perso. Du coup le client affiche 'Slot 2' alors
		// qu'il devrait afficher 'Slot 1'.
		auto* stmt = cache->Acquire(mysql,
			"SELECT slot FROM characters WHERE account_id = ? AND server_id = ? AND deleted_at IS NULL ORDER BY slot ASC");
		if (!stmt)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] FindNextSlot : Acquire prepared stmt a échoué");
			return -1;
		}
		if (!stmt->Bind(0, accountId)) return -1;
		if (!stmt->Bind(1, serverId)) return -1;
		if (!stmt->Execute()) return -1;
		bool used[5]{ false, false, false, false, false };
		while (stmt->FetchRow())
		{
			const int32_t slot = stmt->GetInt32(0, -1);
			if (slot >= 0 && slot < 5)
				used[slot] = true;
		}
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
		auto* stmtCache = guard.cache();
		if (!mysql)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t serverId = ResolveDefaultServerId(mysql, stmtCache);
		if (serverId == 0)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "no target server configured", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (IsForbiddenCharacterName(mysql, stmtCache, parsed->name))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character name is forbidden", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (CharacterNameExistsOnServer(mysql, stmtCache, parsed->name, serverId))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character name already taken on this server", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const int slot = FindNextSlot(mysql, stmtCache, *accountId, serverId);
		if (slot < 0)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "character slots full", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Phase 3.6 — Spawn par défaut lu depuis la config serveur (peut être surchargé
		// plus tard par le shard quand l'utilisateur se déconnecte / le serveur sauvegarde
		// la position courante du personnage). Défauts cohérents avec le défaut client
		// (config.json : client.world.default_spawn).
		const double spawnX     = m_config ? m_config->GetDouble("character_creation.default_spawn.x", 0.0) : 0.0;
		const double spawnY     = m_config ? m_config->GetDouble("character_creation.default_spawn.y", 100.0) : 100.0;
		const double spawnZ     = m_config ? m_config->GetDouble("character_creation.default_spawn.z", 0.0) : 0.0;
		const double spawnYaw   = m_config ? m_config->GetDouble("character_creation.default_spawn.yaw_deg", 0.0) : 0.0;
		const double spawnPitch = m_config ? m_config->GetDouble("character_creation.default_spawn.pitch_deg", -10.0) : -10.0;

		// Phase 3.8 — Persistance des identifiants chaîne race/classe choisis par
		// l'utilisateur (parsed->raceId / parsed->classId, ex. "humains" / "warrior").
		// Limite à 32 chars (taille de la colonne).
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
		// PR2 — validateur d'identifiant de faction provenant du payload client.
		// Accepte : non-vide, uniquement alphanumérique + underscore, longueur [1, 32].
		// Même jeu de caractères que les noms de personnages (IsValidCharacterName),
		// sans contrainte de longueur minimale à 3 — un id court comme "lune" est valide.
		auto isValidFactionId = [](std::string_view id) -> bool {
			if (id.empty() || id.size() > 32u)
				return false;
			for (unsigned char c : id)
			{
				if (!std::isalnum(c) && c != '_')
					return false;
			}
			return true;
		};
		// PR2 — source de la faction : préférer factionId du payload (choix explicite du
		// joueur via l'UI de sélection de faction) ; replier sur la dérivation par race
		// pour les anciens clients ou les races à faction unique non encore sélectionnée.
		// Si factionId est non-vide mais de charset invalide, on replie aussi (lenient) et
		// on loggue un avertissement pour diagnostiquer d'éventuels bugs client.
		std::string factionStr;
		const char* factionSource = nullptr;
		if (!parsed->factionId.empty())
		{
			if (isValidFactionId(parsed->factionId))
			{
				factionStr    = truncate(parsed->factionId);
				factionSource = "payload";
			}
			else
			{
				LOG_WARN(Net, "[CharacterCreateHandler] factionId payload invalide (charset/taille) : '{}' — repli sur dérivation par race",
					parsed->factionId);
				factionStr    = factionFromRace(raceIdTruncated);
				factionSource = "derived(invalid_payload)";
			}
		}
		else
		{
			factionStr    = factionFromRace(raceIdTruncated);
			factionSource = "derived";
		}
		// #1 serveur — genre du personnage (validé male/female, défaut male).
		const std::string genderStr = (parsed->gender == "female") ? "female" : "male";
		// Teinte de peau (skinColorIdx) : entier non signé borné [0, 255] (0 = claire).
		const uint32_t skinColorIdx = static_cast<uint32_t>(parsed->customization.skinColorIdx);

		// N1-B : INSERT via prepared statement. 14 paramètres bind (positions 0-13).
		// Les 4 colonnes hardcodées (race_id=0, class_id=0, level=1, appearance_json='{}')
		// restent en littéraux SQL — pas d'entrée utilisateur.
		// Plus besoin de mysql_real_escape_string : le binding de string_view est safe.
		if (!stmtCache)
		{
			LOG_WARN(Net, "[CharacterCreateHandler] INSERT : cache absent");
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "prepared statement cache unavailable", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto* insertStmt = stmtCache->Acquire(mysql,
			"INSERT INTO characters (account_id, slot, name, server_id, race_id, class_id, level, appearance_json,"
			" spawn_x, spawn_y, spawn_z, spawn_yaw_deg, spawn_pitch_deg, race_str, class_str, faction_str, gender, skin_color_idx) "
			"VALUES (?, ?, ?, ?, 0, 0, 1, '{}', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
		if (!insertStmt
			|| !insertStmt->Bind(0,  *accountId)
			|| !insertStmt->Bind(1,  static_cast<int32_t>(slot))
			|| !insertStmt->Bind(2,  std::string_view(parsed->name))
			|| !insertStmt->Bind(3,  serverId)
			|| !insertStmt->Bind(4,  spawnX)
			|| !insertStmt->Bind(5,  spawnY)
			|| !insertStmt->Bind(6,  spawnZ)
			|| !insertStmt->Bind(7,  spawnYaw)
			|| !insertStmt->Bind(8,  spawnPitch)
			|| !insertStmt->Bind(9,  std::string_view(raceIdTruncated))
			|| !insertStmt->Bind(10, std::string_view(classIdTruncated))
			|| !insertStmt->Bind(11, std::string_view(factionStr))
			|| !insertStmt->Bind(12, std::string_view(genderStr))
			|| !insertStmt->Bind(13, skinColorIdx)
			|| !insertStmt->Execute())
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
		LOG_INFO(Auth, "[CharacterCreateHandler] Character created (account_id={}, character_id={}, server_id={}, slot={}, name={}, race='{}', class='{}', faction='{}', faction_source={}, spawn=({}, {}, {}))",
			*accountId, characterId, serverId, slot, parsed->name, raceIdTruncated, classIdTruncated,
			factionStr.empty() ? "(unaligned)" : factionStr,
			factionSource,
			spawnX, spawnY, spawnZ);
	}
}
