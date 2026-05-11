#pragma once
// CMANGOS.27 (Phase 4.27 step 3+4) -- TradeHandler : dispatch des opcodes
// Trade cote master (83/86/88/91/93). Gere la creation des TradeSession via
// TradeSessionRegistry et envoie les push notifications aux 2 participants
// pour synchroniser leurs UI.
//
// Particularite Trade vs autres tickets : 2 clients connectes simultanement
// participent a la meme session. Chaque transition d'etat (SetOffer / Lock /
// Commit / Cancel) entraine un push au partenaire (et au sender pour la
// reponse), pour qu'il rafraichisse son UI miroir.
//
// Le handler est instancie dans main_linux.cpp, cable via Set*() puis
// enregistre dans le packetHandler du NetServer. Les responses sont emises
// avec le meme requestId / sessionIdHeader que la request recue ; les push
// notifications utilisent requestId=0.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server::trade
{
	class TradeSessionRegistry;
}

namespace engine::server
{
	/// Dispatcher Trade cote master. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class TradeHandler
	{
	public:
		/// Branche le registry des trades actives.
		void SetRegistry(engine::server::trade::TradeSessionRegistry* r) { m_reg = r; }
		/// Branche le NetServer pour pouvoir envoyer les reponses + push.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId. Utilisee pour resoudre
		/// accountId -> connId via Snapshot() (V1 : O(n) au worst-case).
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Trade. Dispatch
		/// vers HandleBegin/HandleSetOffer/HandleLock/HandleCommit/HandleCancel.
		///
		/// \param connId         identifiant de connexion TCP du sender.
		/// \param opcode         opcode du paquet entrant (83/86/88/91/93).
		/// \param requestId      request_id du paquet ; renvoye dans la reponse au sender.
		/// \param sessionIdHeader session_id du paquet ; sert pour la reponse au sender.
		/// \param payload        pointeur sur le payload (sans header).
		/// \param payloadSize    taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

	private:
		/// TRADE_BEGIN : valide self != target, target online, target ni A ni B
		/// dans une autre trade ; cree la session via le registry et push une
		/// BeginNotification au target.
		void HandleBegin(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// TRADE_SET_OFFER : valide la session + l'appartenance, applique
		/// session->SetOffer(...), et push une StateUpdateNotification au
		/// partenaire.
		void HandleSetOffer(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// TRADE_LOCK : valide la session, applique session->Lock(accountId).
		/// Si le state passe a BothLocked, push StateUpdateNotification(BothLocked)
		/// aux 2 (le sender voit aussi le state mis a jour via la response).
		void HandleLock(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// TRADE_COMMIT : valide BothLocked, appelle session->Commit(), push
		/// StateUpdateNotification(Committed) aux 2 et termine la session.
		/// V1 : pas d'application reelle du delta inventory (TODO wallet).
		void HandleCommit(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// TRADE_CANCEL : appelle session->Cancel(accountId), push
		/// CancelNotification aux 2 et termine la session.
		void HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Resout l'accountId -> connId (via Snapshot du connMap + reverse
		/// lookup SessionManager). \return 0 si l'account n'a pas de session
		/// active. Complexite O(n) ou n = nombre de connexions ; OK pour V1.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;

		/// Resout l'accountId -> sessionIdHeader pour pouvoir adresser un push
		/// au bon client. \return 0 si pas de session.
		uint64_t FindSessionIdForAccount(uint64_t accountId) const;

		/// Helper push : envoie un paquet pre-construit au compte si online.
		/// \return true si l'envoi a ete tente (peut quand meme echouer cote
		/// reseau), false si l'account n'est pas online.
		bool SendPacketToAccount(uint64_t accountId, const std::vector<uint8_t>& packet);

		engine::server::trade::TradeSessionRegistry* m_reg        = nullptr;
		NetServer*                                    m_server     = nullptr;
		SessionManager*                               m_sessionMgr = nullptr;
		ConnectionSessionMap*                         m_connMap    = nullptr;
	};
}
