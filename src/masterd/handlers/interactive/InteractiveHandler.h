#pragma once
// M100.32 — InteractiveHandler : dispatch de l'opcode InteractiveStateChange
// (200) côté master + relai sans validation gameplay.
//
// Le handler est instancié dans main_linux.cpp au boot du master, câblé via
// SetXxx(...), seedé depuis l'état initial des objets de la zone (SeedInteractive),
// puis enregistré dans le packetHandler du NetServer pour l'opcode 200.
//
// À réception d'un StateChange :
//   1. Résout connId → session → account (session requise ; sinon drop silencieux).
//   2. Applique au InteractiveStateRelay. Si id inconnu : warning + ignore
//      (PAS de paquet d'erreur — cf. critère d'acceptation).
//   3. Broadcast un StateBroadcast (opcode 201) à toutes les AUTRES sessions
//      actives (l'émetteur a déjà animé localement).
//
// SendInitialSync(connId) envoie un StateSync (opcode 202) avec l'état complet
// de la zone ; appelé quand un client entre en jeu.
//
// AUCUNE validation gameplay : pas de portée, pas de droit d'ouverture, pas
// d'anti-triche.

#include "src/masterd/InteractiveStateRelay.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Dispatcher Interactive Props côté master. Doit être configuré via Set*()
	/// avant tout HandlePacket.
	class InteractiveHandler
	{
	public:
		/// Branche le NetServer pour envoyer broadcasts + sync.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour résoudre sessionId → accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId ↔ sessionId (résolution + broadcast).
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Enregistre un objet interactif avec son état initial (au chargement
		/// de la zone). Thread-safe (pris sous m_mutex).
		void SeedInteractive(uint64_t id, uint8_t initialState);

		/// Point d'entrée appelé par NetServer pour l'opcode 200. Les opcodes
		/// 201/202 sont des pushes serveur→client : jamais reçus ici.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// Envoie l'état complet de la zone (StateSync, opcode 202) au client
		/// `connId`. Appelé quand le client entre en jeu. No-op si server null,
		/// connId=0 ou pas de session pour ce connId.
		/// \return true si le paquet a été envoyé.
		bool SendInitialSync(uint32_t connId);

	private:
		/// Diffuse un StateBroadcast (201) à toutes les sessions actives SAUF
		/// `exceptConn` (l'émetteur, qui a déjà animé localement).
		void BroadcastStateChange(uint32_t exceptConn, uint64_t id, uint8_t newState);

		NetServer*                                m_server     = nullptr;
		SessionManager*                           m_sessionMgr = nullptr;
		ConnectionSessionMap*                     m_connMap    = nullptr;

		/// Protège m_relay.
		mutable std::mutex                        m_mutex;
		engine::server::interactive::InteractiveStateRelay m_relay;
	};
}
