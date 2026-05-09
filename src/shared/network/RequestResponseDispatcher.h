// Dispatcher requête/réponse sur un NetClient.
// Associe chaque réponse réseau à la requête d'origine via request_id, route les push serveur
// (request_id == 0) vers un PushHandler, et déclenche les timeouts à l'expiration.
// Thread-safety : SendRequest() peut être appelé depuis n'importe quel thread ;
//                 Pump() et TickHeartbeat() doivent être appelés depuis le thread principal uniquement.
#pragma once

#include "src/shared/network/NetErrorCode.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/platform/StableMutex.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::network
{
	class NetClient;

	/// Callback déclenché à la réception d'une réponse ou à l'expiration du timeout.
	/// Appelé dans le thread qui exécute Pump() (thread principal).
	/// @param requestId  Identifiant de la requête d'origine.
	/// @param timeout    true si la réponse n'est pas arrivée dans le délai imparti ; payload est alors vide.
	/// @param payload    Corps de la réponse désérialisé (sans en-tête paquet).
	using RequestResponseCallback = std::function<void(uint32_t requestId, bool timeout, std::vector<uint8_t> payload)>;

	/// Callback pour les paquets push serveur (request_id == 0 dans l'en-tête).
	/// Appelé dans le thread qui exécute Pump(). payloadSize peut être 0.
	using PushHandler = std::function<void(uint16_t opcode, const uint8_t* payload, size_t payloadSize)>;

	/// Callback optionnel pour les paquets ERROR reçus du serveur.
	/// Appelé dans le thread qui exécute Pump(), avant de résoudre la requête en attente associée.
	using ErrorHandler = std::function<void(uint32_t requestId, NetErrorCode errorCode, std::string_view message)>;

	/// Dispatcher requête/réponse : fait correspondre les réponses réseau aux requêtes via request_id.
	/// Les paquets push (request_id == 0) sont routés vers le PushHandler.
	/// Les requêtes en attente expirent après timeoutMs (par défaut 5 s) ; le callback reçoit timeout=true.
	/// Pump() doit être appelé régulièrement depuis le thread principal pour traiter les paquets reçus.
	class RequestResponseDispatcher
	{
	public:
		/// @param client Pointeur non-nul vers le NetClient sous-jacent. Doit survivre au dispatcher.
		explicit RequestResponseDispatcher(NetClient* client);
		~RequestResponseDispatcher();

		/// Enregistre le handler pour les paquets push serveur (request_id == 0). Optionnel.
		/// Remplace tout handler précédent. Thread-safe.
		void SetPushHandler(PushHandler handler);

		/// Enregistre le handler pour les paquets ERROR. Optionnel.
		/// Appelé avant la résolution de la requête associée. Thread-safe.
		void SetErrorHandler(ErrorHandler handler);

		/// Envoie une requête et enregistre le callback pour la réponse correspondante.
		/// Le callback est appelé depuis Pump(), pas depuis SendRequest().
		/// @param opcode      Opcode de la requête (voir ProtocolV1Constants.h).
		/// @param payload     Corps sérialisé de la requête (sans en-tête).
		/// @param onResponse  Appelé une seule fois : avec la réponse, ou avec timeout=true après timeoutMs.
		/// @param timeoutMs   Délai avant que le callback soit déclenché avec timeout=true (ms, défaut 5000).
		/// @return false si l'envoi réseau a échoué (callback jamais appelé dans ce cas).
		bool SendRequest(uint16_t opcode, std::span<const uint8_t> payload, RequestResponseCallback onResponse, uint32_t timeoutMs = 5000u);

		/// Consomme les paquets reçus, dispatche les callbacks et expire les requêtes en timeout.
		/// Doit être appelé depuis le thread principal uniquement.
		void Pump();

		/// Définit l'intervalle de heartbeat en secondes (défaut : 30). Appeler avant TickHeartbeat().
		void SetHeartbeatInterval(int64_t intervalSec);

		/// Enregistre le session_id obtenu après authentification.
		/// Les paquets HEARTBEAT l'incluront dans leur en-tête. Thread-safe (atomic).
		void SetSessionId(uint64_t sessionId);

		/// Envoie un HEARTBEAT si l'intervalle configuré est écoulé depuis le dernier envoi.
		/// Sans effet si session_id == 0 ou si le client n'est pas connecté.
		/// Appeler depuis le thread principal, typiquement après Pump().
		void TickHeartbeat();

	private:
		NetClient* m_client;                              ///< Client réseau sous-jacent. Non-propriétaire.
		std::atomic<uint32_t> m_nextRequestId{ 1u };     ///< Compteur monotone des request_id (jamais 0).
		std::atomic<uint64_t> m_sessionId{ 0u };         ///< Session courante ; 0 = non authentifié.
		int64_t m_heartbeatIntervalSec = 30;              ///< Intervalle entre deux HEARTBEAT (secondes).
		std::chrono::steady_clock::time_point m_lastHeartbeatSent{}; ///< Horodatage du dernier HEARTBEAT envoyé.
		engine::platform::StableMutex m_mutex;           ///< Protège m_pending, m_pushHandler, m_errorHandler.

		/// Entrée dans la table des requêtes en attente de réponse.
		struct PendingEntry
		{
			std::chrono::steady_clock::time_point deadline; ///< Expiration au-delà de laquelle timeout=true.
			RequestResponseCallback callback;                ///< Callback à appeler à la réception ou au timeout.
		};
		std::unordered_map<uint32_t, PendingEntry> m_pending; ///< Requêtes en attente, indexées par request_id.
		PushHandler m_pushHandler;   ///< Handler pour les paquets push serveur (request_id == 0).
		ErrorHandler m_errorHandler; ///< Handler optionnel pour les paquets ERROR.
	};
}
