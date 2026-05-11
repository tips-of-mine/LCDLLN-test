/// @file NetServer.h
/// @brief Serveur TCP non-bloquant Linux (epoll) avec chiffrement TLS optionnel (OpenSSL ≥ TLS 1.2).
/// Gère l'acceptation de connexions, le framage RX selon le protocole v1, la file d'envoi TX avec
/// contrôle de flux (token bucket bande passante + taux paquets), et délègue le traitement des
/// paquets complets à un pool de threads workers. Thread-safety : l'ensemble des accès aux
/// connexions est protégé par connMutex ; les méthodes publiques sont sûres depuis n'importe quel
/// thread. Dépendances : OpenSSL (TLS), epoll Linux, engine::network::NetworkBufferPool.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace engine::server
{
	/// Raison normalisée de déconnexion, stable pour les logs et les compteurs d'observabilité.
	/// Chaque valeur correspond à un compteur atomique dans NetServerStats::disconnectByReason.
	enum class DisconnectReason : uint32_t
	{
		PeerClosed,
		EpollErr,
		EpollHup,
		InvalidPacket,
		DecodeFailures,
		RateLimit,
		HandshakeTimeout,
		TlsHandshakeFailed,
		SslReadError,
		SslWriteError,
		RecvError,
		SendError,
		TxQueueCap,
		HeartbeatTimeout,
		/// Connexion fermee a la demande de l'application (ex: SessionManager
		/// cascade kick suite a un duplicate-login). N'est jamais positionnee
		/// par le NetServer lui-meme : seulement par les handlers qui
		/// appellent CloseConnection() depuis le hook OnSessionClosed.
		KickedByDuplicateLogin,
		Count
	};

	/// Paramètres de configuration du NetServer TCP (epoll Linux).
	/// Quand tlsCertPath et tlsKeyPath sont tous deux non-vides, TLS est activé (TLS 1.2 minimum) ;
	/// aucun mode texte clair n'est alors disponible sur ce port.
	struct NetServerConfig
	{
		/// Nombre maximum de connexions simultanées. Toute connexion supplémentaire est rejetée (close immédiat).
		uint32_t maxConnections = 1000u;
		/// Capacité maximale de la file TX par connexion en octets.
		/// Si ce plafond est dépassé lors d'un Send(), la connexion est fermée (backpressure).
		size_t maxQueuedTxBytesPerConnection = 256 * 1024u;
		/// Token bucket TX : débit soutenu maximal en octets/seconde par connexion/joueur.
		/// Configuré via `max_bandwidth_per_player` en Ko/s dans server_config.ini,
		/// converti en octets/s par le point d'entrée. 0 → valeur dérivée automatiquement.
		double maxBandwidthPerPlayerBytesPerSec = 0.0;
		/// Nombre de threads workers chargés du décodage et du dispatch des paquets.
		/// Ces threads ne doivent jamais bloquer l'I/O ; toute opération BDD doit être
		/// effectuée de manière asynchrone ou dans un thread dédié.
		uint32_t workerThreadCount = 4u;
		/// Chemin vers le certificat TLS du serveur (format PEM).
		/// Si non-vide et que tlsKeyPath est aussi renseigné, TLS est activé.
		std::string tlsCertPath;
		/// Chemin vers la clé privée TLS du serveur (format PEM).
		/// Si non-vide et que tlsCertPath est aussi renseigné, TLS est activé.
		std::string tlsKeyPath;
		/// Token bucket RX : taux de paquets soutenu autorisé par connexion (paquets/seconde).
		/// Ex : 200 — un joueur enverrait au maximum 200 paquets complets par seconde.
		double packetRatePerSec = 200.0;
		/// Token bucket RX : capacité maximale du seau (burst initial en tokens).
		/// Ex : 400 — autorise un burst de 400 paquets avant que la limite ne s'applique.
		double packetBurst = 400.0;
		/// Anti-DDoS : nombre maximum de connexions simultanées acceptées depuis la même IP. 0 = désactivé.
		uint32_t maxConnectionsPerIp = 0u;
		/// Anti-DDoS : taux maximal d'acceptation global (accepts/sec via token bucket). 0 = désactivé.
		double maxAcceptsPerSec = 0.0;
		/// Anti-DDoS : nombre d'échecs de handshake TLS avant de bannir temporairement l'IP. 0 = désactivé.
		uint32_t handshakeFailuresBeforeDeny = 0u;
		/// Anti-DDoS : durée du bannissement temporaire après dépassement du seuil d'échecs (en secondes).
		uint32_t handshakeDenyDurationSec = 0u;
		/// Nombre maximal d'échecs de décodage de paquet autorisés avant fermeture de la connexion.
		/// Protège contre les connexions envoyant des données corrompues ou malformées en boucle.
		uint32_t decodeFailureThreshold = 5u;
		/// Délai maximal pour compléter le handshake TLS (en secondes).
		/// La connexion est fermée avec la raison HandshakeTimeout si le délai est dépassé.
		uint32_t handshakeTimeoutSec = 10u;
	};

	/// Callback appelé depuis les threads workers lorsqu'un paquet complet est reçu et validé.
	/// NE PAS effectuer d'opérations bloquantes (BDD, I/O fichier) directement dans ce callback :
	/// cela bloquerait le worker et dégaderait le débit. requestId et sessionId proviennent de
	/// l'en-tête du paquet v1 et doivent être réémis dans les réponses correspondantes.
	/// @param connId      Identifiant opaque de la connexion TCP source.
	/// @param opcode      Opcode du message (cf. MessageKind dans ServerProtocol.h).
	/// @param requestId   Identifiant de requête du paquet, à réinjecter dans la réponse.
	/// @param sessionId   Identifiant de session du paquet (post-authentification), ou 0.
	/// @param payload     Pointeur vers les octets du payload (durée de vie : scope du callback uniquement).
	/// @param payloadSize Taille du payload en octets.
	using NetServerPacketHandler = std::function<void(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionId, const uint8_t* payload, size_t payloadSize)>;

	/// Snapshot of server network counters (M19.11). Thread-safe read via GetNetworkStats().
	struct NetServerStats
	{
		uint64_t connectionsActive = 0;
		uint64_t connectionsTotal = 0;
		uint64_t handshakeSuccess = 0;
		uint64_t handshakeFail = 0;
		uint64_t bytesIn = 0;
		uint64_t bytesOut = 0;
		uint64_t packetsIn = 0;
		uint64_t packetsOut = 0;
		uint64_t packetsDropped = 0;
		uint64_t disconnectByReason[static_cast<size_t>(DisconnectReason::Count)] = {};
	};

	/// Linux-only TCP server: non-blocking epoll, acceptor thread, connection objects (fd, buffers, state),
	/// RX framing (protocol v1 header), TX queue with backpressure, worker pool for packet handling.
	/// EPOLLERR/EPOLLHUP are always handled; never run game/DB logic in the IO thread.
	class NetServer
	{
	public:
		NetServer() = default;
		~NetServer();

		/// Start listening on \a listenPort with \a config. Returns true on success.
		bool Init(uint16_t listenPort, const NetServerConfig& config);

		/// Stop acceptor, close all connections, join workers, release resources.
		void Shutdown();

		/// Enqueue \a packet to send to connection \a connId. Returns false if conn not found or backpressure would be exceeded (connection closed).
		bool Send(uint32_t connId, std::span<const uint8_t> packet);

		/// Close connection by \a connId (e.g. after heartbeat timeout). No-op if connId not found.
		void CloseConnection(uint32_t connId, DisconnectReason reason);

		/// Set handler called from worker threads for each received packet. Optional; not called if unset.
		void SetPacketHandler(NetServerPacketHandler handler);

		/// Current number of connected clients. Thread-safe.
		uint32_t GetConnectionCount() const;

		/// True after a successful Init() and before Shutdown().
		bool IsRunning() const;

		/// Returns disconnect count for \a reason. Thread-safe (atomics). Only valid while server is running.
		uint64_t GetDisconnectCount(DisconnectReason reason) const;

		/// Fills \a out with a snapshot of all network counters. Thread-safe. Call when server is running or after Shutdown for final stats.
		void GetNetworkStats(NetServerStats& out) const;

	private:
		struct Impl;
		Impl* m_impl = nullptr;
	};
}
