#pragma once

/// @file NetClient.h
/// @brief Client TCP non-bloquant avec thread réseau dédié, tramage protocole v1 et file d'événements.
/// @details Gère le cycle de vie complet d'une connexion TCP (résolution DNS, connexion async,
///          lecture partielle avec réassemblage de paquets, file d'envoi non-bloquante).
///          TLS optionnel via OpenSSL avec épinglage de certificat par empreinte SHA-256 (M19.6).
///          Toutes les notifications vers la couche applicative transitent par PollEvents() ;
///          aucun callback utilisateur n'est invoqué depuis le thread réseau.
/// @dependencies ProtocolV1Constants.h (tailles max), StableMutex (synchronisation inter-thread),
///               OpenSSL (TLS), Winsock2 (I/O réseau Windows).
/// @thread-safety PollEvents() et Send() sont thread-safe (protégés par m_mutex).
///                Connect() et Disconnect() peuvent être appelés depuis n'importe quel thread.
///                Les compteurs (GetBytes*/GetPackets*) sont atomiques.

#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/platform/StableMutex.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine::network
{
	/// @brief États possibles de la machine à états du client TCP.
	/// @details La transition suit toujours le chemin :
	///          Disconnected -> Connecting -> Connected -> Disconnecting -> Disconnected.
	///          L'état est mis à jour exclusivement par le thread réseau (m_state est atomique).
	enum class NetClientState
	{
		Disconnected,  ///< Aucune connexion active ; état initial et état après déconnexion.
		Connecting,    ///< Résolution DNS ou handshake TCP/TLS en cours.
		Connected,     ///< Canal TCP (et TLS si activé) établi ; envoi/réception opérationnels.
		Disconnecting  ///< Déconnexion demandée, en attente de flush ou de fin de handshake.
	};

	/// @brief Type d'événement livré au thread principal via PollEvents().
	/// @details Les callbacks utilisateur ne doivent jamais être invoqués depuis le thread réseau ;
	///          ce mécanisme garantit que tout traitement applicatif se fait côté appelant de Pump()/PollEvents().
	enum class NetClientEventType
	{
		Connected,       ///< Connexion TCP (et TLS) établie avec succès.
		Disconnected,    ///< Connexion perdue ou fermée volontairement ; voir NetClientEvent::reason.
		PacketReceived   ///< Un paquet protocole v1 complet a été reçu ; voir NetClientEvent::packet.
	};

	/// @brief Événement réseau produit par le thread réseau et consommé via PollEvents().
	/// @details Un seul événement est produit par changement d'état ou par paquet reçu.
	///          Le champ pertinent dépend du type :
	///          - Connected    -> reason vide, packet vide.
	///          - Disconnected -> reason contient la cause (erreur ou "user request"), packet vide.
	///          - PacketReceived -> packet contient l'intégralité du paquet (en-tête + payload),
	///            reason vide.
	struct NetClientEvent
	{
		NetClientEventType type = NetClientEventType::Disconnected;

		/// @brief Cause de la déconnexion (non vide uniquement pour Disconnected).
		/// @details Exemples : "peer closed", "recv error", "TLS handshake timeout",
		///          "certificate fingerprint mismatch", "user request".
		std::string reason;

		/// @brief Paquet protocole v1 complet (en-tête + payload) pour PacketReceived.
		/// @details Garanti valide (ParseResult::Ok) au moment de la mise en file.
		///          Vide pour les autres types d'événements.
		std::vector<uint8_t> packet;
	};

	/// @brief Client TCP non-bloquant avec thread réseau dédié, tramage protocole v1 et file d'événements.
	/// @details Le thread réseau tourne en boucle à ~200 Hz (sleep 5 ms) et gère :
	///          - La connexion TCP asynchrone avec sélect() et timeout 10 s.
	///          - L'éventuel handshake TLS (TLS 1.2+ minimum, épinglage SHA-256 obligatoire
	///            sauf mode allow_insecure_dev).
	///          - La lecture partielle avec réassemblage de paquets via PacketView.
	///          - L'envoi non-bloquant paquet par paquet depuis m_writeQueue (max 1024 entrées).
	///          - La détection de déconnexion distante (recv == 0 ou erreur SSL).
	///          Les événements sont produits dans m_eventQueue (protégée par m_mutex) et
	///          consommés par PollEvents() depuis le thread principal.
	/// @note Ne jamais appeler PollEvents() depuis le thread réseau.
	class NetClient
	{
	public:
		/// @brief Construit le client dans l'état Disconnected. N'alloue pas de thread réseau.
		NetClient();

		/// @brief Détruit le client. Signale m_quit, attend la fin du thread réseau (join bloquant).
		/// @note Bloquant si le thread réseau est actif. Ferme proprement la socket et libère TLS.
		~NetClient();

		/// @brief Définit l'empreinte SHA-256 attendue du certificat serveur (format hex, 64 caractères).
		/// @details Si non vide, active TLS sur la prochaine connexion. L'empreinte est normalisée
		///          en minuscules ; les caractères non-hexadécimaux sont ignorés. Si la chaîne ne
		///          produit pas exactement 64 hex après normalisation, la connexion TLS sera refusée.
		/// @param hexFingerprint Empreinte SHA-256 du DER du certificat, en hexadécimal
		///                       (insensible à la casse, espaces ignorés).
		/// @note Doit être appelé avant Connect(). Thread-safe (protégé par m_mutex).
		void SetExpectedServerFingerprint(std::string hexFingerprint);

		/// @brief Active le mode développement non-sécurisé (désactive la vérification d'empreinte).
		/// @details Uniquement pour les environnements de développement local où le certificat
		///          n'est pas encore épinglé. Un avertissement est loggé à chaque connexion.
		/// @param allow true = vérification désactivée, false (défaut) = épinglage obligatoire.
		/// @note Thread-safe (protégé par m_mutex).
		void SetAllowInsecureDev(bool allow);

		/// @brief Initie une connexion asynchrone vers host:port.
		/// @details Retourne immédiatement. Le résultat est livré via PollEvents() :
		///          un événement Connected en cas de succès, ou Disconnected avec une raison en cas d'échec.
		///          Ignoré si l'état n'est pas Disconnected (avertissement loggé).
		///          Lance le thread réseau s'il n'est pas encore démarré.
		///          Timeout de connexion : 10 secondes.
		/// @param host Nom d'hôte ou adresse IP du serveur (résolution DNS si nécessaire).
		/// @param port Port TCP du serveur (1-65535).
		/// @note Peut être appelé depuis n'importe quel thread.
		void Connect(const std::string& host, uint16_t port);

		/// @brief Demande une déconnexion propre.
		/// @details Positionne m_requestDisconnect = true. Le thread réseau fermera la socket
		///          (SSL_shutdown si TLS actif) et poussera un événement Disconnected avec la raison fournie.
		/// @param reason Message décrivant la cause de la déconnexion (visible dans l'événement Disconnected).
		/// @note Peut être appelé depuis n'importe quel thread. Non bloquant.
		void Disconnect(std::string reason);

		/// @brief Met un paquet en file d'envoi (copie le buffer, non-bloquant).
		/// @details Le paquet doit être un paquet protocole v1 complet (en-tête + payload).
		///          L'envoi effectif est réalisé par le thread réseau lors de sa prochaine itération.
		/// @param packet Vue sur les octets du paquet (kProtocolV1HeaderSize <= taille <= kProtocolV1MaxPacketSize).
		/// @return false si non connecté, si la taille du paquet est invalide, ou si la file est pleine (>= 1024 entrées).
		/// @note Thread-safe (protégé par m_mutex).
		bool Send(std::span<const uint8_t> packet);

		/// @brief Retourne et vide la file d'événements produite par le thread réseau.
		/// @details Doit être appelé depuis le thread principal uniquement (ou au moins depuis un seul thread).
		///          Transfère la propriété des événements (move) ; la file interne est ensuite vide.
		/// @return Vecteur d'événements dans l'ordre de production (FIFO). Vide si aucun événement en attente.
		/// @note Thread-safe (protégé par m_mutex).
		std::vector<NetClientEvent> PollEvents();

		/// @brief Retourne l'état courant de la connexion.
		/// @details L'état est mis à jour par le thread réseau via un atomique ; la lecture est cohérente
		///          mais peut être légèrement décalée par rapport aux événements dans la file.
		/// @return État courant (Disconnected, Connecting, Connected ou Disconnecting).
		NetClientState GetState() const;

		/// @brief Retourne le nombre total d'octets reçus depuis la dernière construction. Thread-safe (atomique).
		uint64_t GetBytesIn() const;
		/// @brief Retourne le nombre total d'octets envoyés depuis la dernière construction. Thread-safe (atomique).
		uint64_t GetBytesOut() const;
		/// @brief Retourne le nombre total de paquets complets reçus depuis la dernière construction. Thread-safe (atomique).
		uint64_t GetPacketsIn() const;
		/// @brief Retourne le nombre total de paquets complètement envoyés depuis la dernière construction. Thread-safe (atomique).
		uint64_t GetPacketsOut() const;

	private:
		/// @brief Point d'entrée du thread réseau. Boucle principale : connect, read, write, disconnect.
		/// @details Initialise WSA, gère les connexions TCP/TLS asynchrones, le tramage RX
		///          via PacketView, et la file d'envoi TX. Se termine quand m_quit == true.
		void NetworkThreadRun();

		/// @brief Ferme proprement une session TLS et déconnecte le socket.
		/// @details Appelle SSL_shutdown, SSL_free, SSL_CTX_free (si non null), CloseSocket,
		///          passe l'état à Disconnected, et pousse un événement Disconnected dans m_eventQueue.
		/// @param ssl    Pointeur SSL* à fermer (peut être null si le handshake n'a pas démarré).
		/// @param ctx    Pointeur SSL_CTX* à libérer (peut être null).
		/// @param socketHandle Handle de socket (réinitialisé à 0 après fermeture).
		/// @param reason Raison de la déconnexion transmise dans l'événement.
		void TlsCleanupAndDisconnect(void* ssl, void* ctx, uintptr_t& socketHandle, std::string_view reason);

		/// @brief État courant de la connexion. Écrit par le thread réseau, lu par n'importe quel thread.
		std::atomic<NetClientState> m_state{ NetClientState::Disconnected };

		/// @brief Compteurs de trafic réseau (atomiques, thread-safe). Initiaux à 0.
		std::atomic<uint64_t> m_bytesIn{ 0 };
		std::atomic<uint64_t> m_bytesOut{ 0 };
		std::atomic<uint64_t> m_packetsIn{ 0 };
		std::atomic<uint64_t> m_packetsOut{ 0 };

		/// @brief Mutex protégeant tous les membres partagés entre le thread principal et le thread réseau.
		engine::platform::StableMutex m_mutex;

		/// @brief Hôte et port cibles pour la prochaine connexion (protégés par m_mutex).
		std::string m_pendingHost;
		uint16_t m_pendingPort = 0;

		/// @brief Indique qu'une connexion est en attente de traitement par le thread réseau (protégé par m_mutex).
		bool m_pendingConnect = false;

		/// @brief Indique qu'une déconnexion a été demandée ; le thread réseau la traitera à sa prochaine itération.
		bool m_requestDisconnect = false;

		/// @brief Raison de la déconnexion demandée (protégée par m_mutex).
		std::string m_disconnectReason;

		/// @brief Empreinte SHA-256 attendue du certificat serveur en hexadécimal (protégée par m_mutex).
		/// @details Non vide = TLS activé. Vide = connexion TCP nue (non chiffrée).
		std::string m_expectedServerFingerprintHex;

		/// @brief Si true, la vérification d'empreinte TLS est ignorée (mode développement uniquement).
		bool m_allowInsecureDev = false;

		/// @brief File de paquets à envoyer (chaque entrée est un paquet protocole v1 complet).
		/// @details Protégée par m_mutex. Limitée à kMaxWriteQueueSize (1024) entrées.
		///          Le thread réseau consomme un paquet à la fois, avec reprise sur envoi partiel.
		std::vector<std::vector<uint8_t>> m_writeQueue;

		/// @brief File d'événements produits par le thread réseau, consommés par PollEvents() (protégée par m_mutex).
		std::vector<NetClientEvent> m_eventQueue;

		/// @brief Thread réseau dédié (démarré par Connect(), joint par le destructeur).
		std::thread m_networkThread;

		/// @brief Signal d'arrêt du thread réseau. Positionné à true par le destructeur.
		std::atomic<bool> m_quit{ false };
	};
}
