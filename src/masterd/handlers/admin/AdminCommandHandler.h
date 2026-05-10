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
// Commandes dispatchees V1 (Wave 1) :
//   - "/sky moon <phase>"   admin   (pilot)
//   - "/sky time <hours>"   admin
//   - "/sky info"           player  (audit-only)
//   - "/loot"               admin   (UI toggle + audit)
//   - "/promote <id> <role>" admin   (mise a jour role via AccountRoleService)
// Pour toute autre commande connue du registre, la reponse est un Ok stub
// avec result vide. Les futures PR ajouteront les dispatchers specifiques
// (kick, ban, mute, etc.).

#include <cstddef>
#include <cstdint>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
	class AccountRoleService;
	class SlashCommandRegistry;
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

		NetServer*            m_server      = nullptr;
		SessionManager*       m_sessionMgr  = nullptr;
		ConnectionSessionMap* m_connMap     = nullptr;
		AccountRoleService*   m_roleService = nullptr;
		SlashCommandRegistry* m_registry    = nullptr;
	};
}
