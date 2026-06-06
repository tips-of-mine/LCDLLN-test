#pragma once
// AdminCommandHandler : dispatcher central pour TOUTES les slash commands
// avec RBAC + audit log. Pattern strict comme LunarHandler / GameEventHandler.
//
// Ce handler traite les opcodes 195/196 (cf. ProtocolV1Constants.h). Il
// effectue les verifications dans cet ordre :
//   1. Auth (session valide + accountId resolu) -> sinon Unauthorized.
//   2. Lookup de la commande dans le registre   -> sinon UnknownCommand.
//   3. Verification minRole vs role utilisateur -> sinon Denied.
//   4. Dispatch sur le nom de commande pour appliquer la logique metier.
//
// Tous les chemins (succes / refus / erreur) emettent un log Audit avec
// le format defini dans docs/slash_commands_rbac.md. Le subsystem est
// "Audit" si le logger le supporte, sinon "Auth" en fallback (le
// LOG_INFO macro utilise une string runtime, pas une enum, donc
// n'importe quel nom est accepte).
//
// Commandes dispatchees (Wave 1 + Wave 2) :
//   Wave 1 (debug + admin tool) :
//     - "/sky moon <phase>"      admin        (pilot)
//     - "/sky time <hours>"      admin
//     - "/sky info"              player       (audit-only)
//     - "/loot"                  admin        (UI toggle + audit)
//     - "/promote <id> <role>"   admin        (AccountRoleService::SetRole)
//   Wave 2 (moderation) :
//     - "/who"                   player       (liste joueurs connectes)
//     - "/report <player>"       player       (cree un ticket GM)
//     - "/kick <player>"         moderator    (deconnecte un joueur)
//     - "/mute <player> <min>"   moderator    (chat_mutes via DB)
//     - "/ban <player> <reason>" game_master  (account_status=Locked + kick)
//     - "/announce <message>"    game_master  (broadcast canal Server)
//   Wave 3 (UI panels log-only) : /mail, /quest, /guild, /ah, /skills, /lfg,
//     /arena, /bg, /pvp, /weather, /events, /rep, /ticket
//   Wave 16 (debug PacketLog on-demand) :
//     - "/packetlog status"             admin   (etat ring buffer)
//     - "/packetlog dump <connId> [n]"  admin   (dump entries d'une conn)
//     - "/packetlog dump_all [n]"       admin   (dump global)
//
// Pour toute autre commande connue du registre, la reponse est un Ok stub
// avec result vide. Les futures PR ajouteront les dispatchers specifiques.

#include "src/shared/network/AdminCommandPayloads.h"

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class AccountRoleService;
	class AccountStore;
	class SlashCommandRegistry;
	class WorldClockHandler;
}

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::gmtickets
{
	class GmTicketSystem;
}

namespace engine::server::netdebug
{
	class PacketLog;
}

namespace engine::server
{
	/// Dispatcher central des slash commands cote master.
	/// Doit etre configure via Set*() avant tout HandlePacket. Si l'un des
	/// pointeurs critique n'est pas cable (server, sessionMgr, connMap,
	/// registry), le handler refusera toutes les commandes (Unauthorized
	/// ou ServerError selon le manque). C'est defensif : on n'execute
	/// jamais sans contexte complet.
	class AdminCommandHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer la reponse 196.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* mgr) { m_sessionMgr = mgr; }
		/// Branche la map connId -> sessionId pour valider l'auth.
		void SetConnectionSessionMap(ConnectionSessionMap* m) { m_connMap = m; }
		/// Branche le service de role pour lire la valeur courante de
		/// accounts.role. Si nullptr, le handler considere TOUS les comptes
		/// comme Player (V1 fallback : commandes minRole>Player refusees).
		void SetAccountRoleService(AccountRoleService* s) { m_roleService = s; }
		/// Branche le registre des commandes (charge depuis slash_commands.json).
		void SetSlashCommandRegistry(SlashCommandRegistry* r) { m_registry = r; }

		/// Branche l'AccountStore pour resoudre login -> account_id
		/// (utilise par /kick, /mute, /ban, /report). Si nullptr, ces
		/// commandes repondront ServerError.
		void SetAccountStore(AccountStore* store) { m_accountStore = store; }

		/// Branche le GmTicketSystem pour creer des tickets via /report.
		/// Si nullptr, /report repondra ServerError.
		void SetGmTicketSystem(engine::server::gmtickets::GmTicketSystem* sys) { m_gmTicketSys = sys; }

		/// Branche le pool MySQL pour /mute (INSERT chat_mutes). Si nullptr,
		/// /mute repondra ServerError.
		void SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_dbPool = pool; }

		/// Branche le ring buffer PacketLog (Wave 12) pour les commandes
		/// /packetlog status, /packetlog dump, /packetlog dump_all. Si
		/// nullptr (cas server.debug.packetlog.enabled=false), les
		/// commandes repondront Ok avec un message "PacketLog disabled".
		void SetPacketLog(engine::server::netdebug::PacketLog* log) { m_packetLog = log; }

		/// Branche le WorldClockHandler (etat horloge monde authoritative)
		/// pour les commandes admin /settime, /pausetime, /settimescale.
		/// Si nullptr, ces commandes repondront ServerError.
		void SetWorldClockHandler(engine::server::WorldClockHandler* h) { m_worldClock = h; }

		/// Dispatch packet : traite uniquement opcode 195
		/// (kOpcodeAdminCommandRequest). Pour tout autre opcode, no-op.
		///
		/// \param connId           identifiant TCP de la connexion (Send response).
		/// \param opcode           opcode du paquet entrant (doit etre 195).
		/// \param requestId        request_id du paquet entrant ; renvoye dans la reponse.
		/// \param sessionIdHeader  session_id du paquet entrant ; renvoye dans la reponse.
		/// \param payload          pointeur sur le payload (sans header).
		/// \param payloadSize      taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite ADMIN_COMMAND_REQUEST : parse, auth, lookup, role check,
		/// dispatch metier, audit log, send response.
		void HandleRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		                   const uint8_t* payload, size_t payloadSize);

		/// Dispatch "/who" : enumere les sessions actives, resout
		/// account_id -> login via AccountStore, et remplit \p resp avec
		/// "count=N" + "logins=alice,bob,..." en compact CSV.
		/// \param resp Reponse a remplir (status sera Ok meme si liste vide).
		void DispatchWho(engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/report <player>" : valide args, resout target par
		/// login, et cree un ticket GM via GmTicketSystem avec
		/// actorAccountId comme reporter.
		/// \param actorAccountId  Compte qui execute la commande.
		/// \param args            args[0] = nom du joueur cible.
		/// \param resp            Reponse a remplir (status Ok / InvalidArgs / ServerError).
		void DispatchReport(uint64_t actorAccountId,
		                    const std::vector<std::string>& args,
		                    engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/kick <player>" : verifie role inferieur strict
		/// (un mod ne kick pas un mod), trouve la connexion via la
		/// snapshot connMap, puis CloseConnection.
		/// \param actorAccountId  Compte qui execute la commande.
		/// \param args            args[0] = nom du joueur cible.
		/// \param resp            Reponse a remplir.
		void DispatchKick(uint64_t actorAccountId,
		                  const std::vector<std::string>& args,
		                  engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/mute <player> <duration_minutes>" : verifie role
		/// inferieur strict, INSERT/REPLACE dans la table chat_mutes avec
		/// until_ts = NowUnixMsUtc() + duration_minutes * 60_000.
		/// \param actorAccountId  Compte qui execute la commande.
		/// \param args            args[0] = nom joueur, args[1] = duree en minutes (>0).
		/// \param resp            Reponse a remplir.
		void DispatchMute(uint64_t actorAccountId,
		                  const std::vector<std::string>& args,
		                  engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/ban <player> <reason>" : verifie role inferieur,
		/// UPDATE accounts.status=Locked via AccountStore::SetAccountStatus,
		/// puis deconnecte le compte si online.
		/// \param actorAccountId  Compte qui execute la commande.
		/// \param args            args[0] = nom joueur, args[1..] = raison libre.
		/// \param resp            Reponse a remplir.
		void DispatchBan(uint64_t actorAccountId,
		                 const std::vector<std::string>& args,
		                 engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/announce <message>" : broadcast un ChatRelay
		/// (channel=Server, sender="[Announce] <actorLogin>") a toutes
		/// les sessions actives via la snapshot connMap.
		/// \param actorAccountId  Compte qui execute la commande (pour login dans sender).
		/// \param args            args[0..] = message complet (concatene si plusieurs).
		/// \param resp            Reponse a remplir.
		void DispatchAnnounce(uint64_t actorAccountId,
		                      const std::vector<std::string>& args,
		                      engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch des trois sous-commandes "/packetlog status",
		/// "/packetlog dump <connId> [n]" et "/packetlog dump_all [n]".
		/// Wave 16 : si m_packetLog == nullptr, retourne Ok avec un message
		/// "PacketLog disabled". Sinon, formate les entries demandees et
		/// les inscrit dans resp.result (tronquees a ~4 KB).
		/// \param subCommand  Identifiant de la sous-commande : "status",
		///                    "dump" ou "dump_all" (extrait du nom canonique
		///                    "/packetlog <sub>").
		/// \param args        Arguments positionnels apres la sous-commande
		///                    (connId pour dump, n optionnel partout).
		/// \param resp        Reponse a remplir.
		void DispatchPacketLog(const std::string& subCommand,
		                       const std::vector<std::string>& args,
		                       engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/settime <HH:MM>" : parse l'heure (split sur ':'),
		/// valide 0<=h<24 et 0<=m<60, convertit en hours flottant, puis
		/// applique via WorldClockHandler::SetTimeOfDay (broadcast 205).
		/// \param args args[0] = heure au format "HH:MM".
		/// \param resp Reponse a remplir (Ok / InvalidArgs / ServerError).
		void DispatchSetTime(const std::vector<std::string>& args,
		                     engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/pausetime <on|off>" : gele (on) ou reprend (off)
		/// l'horloge monde via WorldClockHandler::SetPaused. Argument hors
		/// {on,off} -> InvalidArgs.
		/// \param args args[0] = "on" ou "off".
		/// \param resp Reponse a remplir.
		void DispatchPauseTime(const std::vector<std::string>& args,
		                       engine::network::admin::AdminCommandResponse& resp);

		/// Dispatch "/settimescale <minutes>" : change la vitesse du cycle
		/// jour/nuit via WorldClockHandler::SetTimeScale (borne [1,1440] cote
		/// handler). Argument non numerique ou hors bornes -> InvalidArgs.
		/// \param args args[0] = minutes reelles par jour de jeu (1..1440).
		/// \param resp Reponse a remplir.
		void DispatchSetTimeScale(const std::vector<std::string>& args,
		                          engine::network::admin::AdminCommandResponse& resp);

		NetServer*            m_server      = nullptr;
		SessionManager*       m_sessionMgr  = nullptr;
		ConnectionSessionMap* m_connMap     = nullptr;
		AccountRoleService*   m_roleService = nullptr;
		SlashCommandRegistry* m_registry    = nullptr;
		AccountStore*         m_accountStore = nullptr;
		engine::server::gmtickets::GmTicketSystem* m_gmTicketSys = nullptr;
		engine::server::db::ConnectionPool*        m_dbPool      = nullptr;
		engine::server::netdebug::PacketLog*       m_packetLog   = nullptr;
		engine::server::WorldClockHandler*         m_worldClock  = nullptr;
	};
}
