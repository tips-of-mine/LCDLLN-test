#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — GuildHandler : dispatch des
// opcodes Guild cote joueur (164/166/168/170) et registry in-memory V1
// de 2 guildes hardcodees ("Les Gardiens" + "L'Ombre") avec leurs membres,
// bank tab 0 et permissions WoW defaults par rang.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// 4 requests opcodes. Les responses 165/167/169/171 sont emises avec le
// meme requestId / sessionId que la request recue. La push notification 172
// (MotdUpdate) est emise par le handler (helper public PushMotdUpdate)
// pour signaler les changements de MOTD a un client donne.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 1) dans la reponse type-specific.
//
// Store in-memory V1 (mutex protege) :
//   - Guild 1 "Les Gardiens" leader=Aragorn motd="Soyez courageux"
//       members : Aragorn(0/GM, online), Legolas(1/Officer, online),
//                 Gimli(5/Member, offline), Frodo(9/Initiate, offline)
//       bank0 : Iron Ore x100, Linen Cloth x250, Mageweave x80,
//               Health Potion x30, Mana Potion x20
//   - Guild 2 "L'Ombre" leader=Saruman motd="Le pouvoir est tout"
//       members : Saruman(0/GM, online), Wormtongue(5/Member, offline)
//       bank0 : Black Cloth x50, Soul Shard x10
//   - GuildPermissionMatrix : SetupWowDefaults(1) + SetupWowDefaults(2).
//   - Rank names array : 10 ranks (0=Guild Master ... 9=Initiate).
//
// V1 limitations :
//   - 2 guildes hardcodees. Future PR : DB seed via MysqlGuildStore.
//   - Pas de filtrage par account membership : n'importe quel client peut
//     consulter n'importe quelle guilde V1 (lecture seule globale). Vraie
//     ACL via la matrice GuildPermissionMatrix viendra plus tard.
//   - Bank tab 0 only.
//   - Pas de modification client (Invite/Remove/Promote/Demote V1 read-only).
//   - Pas de SyncGuilds RPC entre master et shardd (master autoritaire V1).

#include "src/shardd/guild/GuildPermissionMatrix.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Membre in-memory d'une guilde V1.
	struct InMemoryGuildMember
	{
		std::string accountName;
		uint8_t     rankId = 0;
		bool        online = false;
	};

	/// Item in-memory de la bank tab 0 V1.
	struct InMemoryGuildBankItem
	{
		uint32_t    slotIndex = 0;
		std::string itemName;
		uint32_t    count     = 0;
	};

	/// Guilde in-memory V1.
	struct InMemoryGuild
	{
		uint32_t                            guildId = 0;
		std::string                         name;
		std::string                         motd;
		std::string                         leaderName;
		std::vector<InMemoryGuildMember>    members;
		std::vector<InMemoryGuildBankItem>  bank0;
	};

	/// Dispatcher Guild cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class GuildHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Initialise le store V1 : enregistre les 2 guildes hardcodees
		/// ("Les Gardiens", "L'Ombre") avec leurs membres + bank + perms WoW.
		/// Idempotent : appelable a chaque boot.
		void SeedV1Guilds();

		/// Point d'entree appele par NetServer pour les opcodes Guild.
		/// Dispatch vers HandleList / HandleMembers / HandlePermissions /
		/// HandleBank selon l'opcode. Si l'opcode n'est pas un opcode Guild,
		/// ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (164/166/168/170).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push GuildMotdUpdateNotification (opcode 172)
		/// au client identifie par \p connId. Utilise par le handler en interne
		/// mais accessible egalement depuis l'exterieur (tests, hooks, future
		/// PR /guild motd <text>).
		///
		/// \param connId    identifiant de connexion TCP cible (0 = no-op).
		/// \param guildId   identifiant de la guilde dont le MOTD change.
		/// \param newMotd   nouveau MOTD (UTF-8).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushMotdUpdate(uint32_t connId, uint32_t guildId, const std::string& newMotd);

		/// Helper static : retourne le nom du rang WoW par defaut pour un rankId
		/// (0=Guild Master ... 9=Initiate). \p rankId hors plage retourne "?".
		static const char* RankName(uint8_t rankId);

	private:
		/// Traite GUILD_LIST_REQUEST : retourne les 2 guildes seedees V1 + summary.
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GUILD_MEMBERS_REQUEST : valide guildId puis renvoie la liste
		/// des membres avec rankName resolu.
		void HandleMembers(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GUILD_PERMISSIONS_REQUEST : valide guildId puis itere les rangs
		/// 0-9 et renvoie les masks via GuildPermissionMatrix::GetMask.
		void HandlePermissions(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite GUILD_BANK_REQUEST : valide guildId + tabIndex (0 V1) puis
		/// renvoie la liste des items du tab 0.
		void HandleBank(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Recherche dans m_guilds la guilde guildId (sans verrouiller m_mutex —
		/// l'appelant doit deja le tenir).
		const InMemoryGuild* FindGuildLocked(uint32_t guildId) const;

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Mutex protegeant m_guilds + m_perms + m_seeded.
		mutable std::mutex                               m_mutex;

		/// Registry guildes in-memory (V1, 2 guildes seedees).
		std::vector<InMemoryGuild>                       m_guilds;

		/// Matrice de permissions par rang. Defaults WoW seedes au boot pour
		/// les 2 guildes V1.
		engine::server::guild::GuildPermissionMatrix     m_perms;

		/// True une fois SeedV1Guilds() appele avec succes.
		bool                                             m_seeded = false;
	};
}
