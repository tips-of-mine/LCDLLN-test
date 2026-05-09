#pragma once
// CMANGOS.18 (Phase 3.18 step 3) — MailHandler : dispatch des opcodes Mail
// (49–58) et appel des methodes correspondantes de MailManager.
//
// Le handler est instancié dans main_linux.cpp au boot du master, câblé via
// SetXxx(...), puis enregistré dans le packetHandler du NetServer pour les
// opcodes 49/51/53/55/57 (les requests). Les responses sont émises avec
// le même requestId / sessionId que la request reçue (cf. RequestResponseDispatcher
// pour le contrat client).
//
// Validation session : chaque opcode exige une session authentifiée. Le
// handler résout connId → sessionId via ConnectionSessionMap, puis sessionId
// → accountId via SessionManager. Si l'un échoue, on répond avec
// error=Unauthorized (pas un ErrorPacket — pour le client UI le code 6
// dans la réponse type-specific est plus exploitable qu'un BAD_REQUEST
// générique).

#include <cstdint>
#include <cstddef>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::mail
{
	class MailManager;
}

namespace engine::server
{
	/// Dispatcher Mail. Doit être configuré via Set*() avant tout HandlePacket.
	class MailHandler
	{
	public:
		/// Branche le manager de mails (logique métier + store).
		void SetMailManager(engine::server::mail::MailManager* mgr) { m_mgr = mgr; }
		/// Branche le NetServer pour pouvoir envoyer les réponses.
		void SetServer(NetServer* server) { m_server = server; }
		/// Branche le SessionManager pour résoudre sessionId → accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId → sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entrée appelé par NetServer pour les opcodes Mail. Dispatch
		/// vers HandleSend/HandleListInbox/etc. selon l'opcode. Si l'opcode
		/// n'est pas un opcode Mail, ignore silencieusement (filtrage déjà
		/// fait côté main_linux).
		///
		/// \param connId         identifiant de connexion TCP (pour Send response).
		/// \param opcode         opcode du paquet entrant (49/51/53/55/57).
		/// \param requestId      request_id du paquet entrant ; renvoyé tel quel dans la réponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoyé tel quel dans la réponse.
		/// \param payload        pointeur sur le payload (sans header).
		/// \param payloadSize    taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite MAIL_SEND_REQUEST : parse + appel MailManager::Send + emit response.
		void HandleSend(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t senderAccountId, const uint8_t* payload, size_t payloadSize);

		/// Traite MAIL_LIST_INBOX_REQUEST : appel MailManager::Inbox + emit response
		/// avec la liste convertie en MailInboxEntry.
		void HandleListInbox(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite MAIL_READ_REQUEST : marque comme lu + retourne le body.
		void HandleRead(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite MAIL_TAKE_ATTACHMENTS_REQUEST : retire le gold (et valide COD).
		/// Cette PR ne câble pas les items attachés (cf. step 3.b). On retire
		/// uniquement le gold et on retourne le montant pris.
		void HandleTakeAttachments(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite MAIL_DELETE_REQUEST : appel MailManager::Delete et map les codes erreur.
		void HandleDelete(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		engine::server::mail::MailManager* m_mgr        = nullptr;
		NetServer*                          m_server     = nullptr;
		SessionManager*                     m_sessionMgr = nullptr;
		ConnectionSessionMap*               m_connMap    = nullptr;
	};
}
