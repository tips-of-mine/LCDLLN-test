#pragma once
// ETAPE3.1 (chantier 4.3) — chiffrement et authentification des datagrammes UDP
// de jeu (client <-> shard).
//
// Principe :
//   - À chaque EnterWorld, le client tire une clé de session aléatoire de 32 octets
//     (GenerateUdpSessionKey). Elle part au master dans la requête EnterWorld (canal
//     TLS), puis du master au shard dans le push d'admission (canal TLS). Elle ne
//     circule jamais en clair.
//   - À chaque ouverture de socket UDP, le client tire un sel de 16 octets. Les deux
//     côtés dérivent la clé de travail : HKDF-SHA256(clé de session, sel,
//     "LCDLLN UDP v1"). Une nouvelle socket = une nouvelle clé de travail, donc les
//     compteurs peuvent repartir de zéro sans jamais réutiliser un nonce.
//   - Chaque datagramme est scellé en ChaCha20-Poly1305 (AEAD) : contenu chiffré,
//     en-tête authentifié (données associées), étiquette de 16 octets.
//   - Nonce (12 octets) = sens (4 octets : "C2S\0" ou "S2C\0") + compteur 64 bits.
//     Le sens sépare les deux flux qui partagent la clé de travail.
//   - Le destinataire refuse tout compteur déjà vu ou trop ancien (fenêtre de
//     64 paquets) : un datagramme capturé puis renvoyé est rejeté.
//
// Formats (petit-boutiste) :
//   Poignée de main (client -> shard, porte le Hello) :
//     magic u32 | type u8 = 1 | character_id u64 | sel [16] | chiffré | étiquette [16]
//     compteur implicite 0 dans le sens C2S ; données associées = les 29 premiers octets.
//   Données (les deux sens) :
//     magic u32 | type u8 = 2 | compteur u64 | chiffré | étiquette [16]
//     compteur >= 1 ; données associées = les 13 premiers octets.
//
// Le contenu chiffré est un paquet du protocole de jeu habituel (ServerProtocol),
// inchangé : le chiffrement est une enveloppe, pas un nouveau protocole.
//
// Aucune dépendance au réseau ni à la boucle de jeu : testable seul
// (cf. udp_secure_channel_tests).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::network
{
	/// Taille de la clé de session échangée à l'EnterWorld (octets).
	inline constexpr size_t kUdpSessionKeySize = 32u;
	/// Taille du sel tiré à chaque ouverture de socket (octets).
	inline constexpr size_t kUdpSessionSaltSize = 16u;
	/// Taille de l'étiquette d'authentification Poly1305 (octets).
	inline constexpr size_t kUdpAeadTagSize = 16u;

	/// Marque d'une enveloppe chiffrée ("LCDE" en mémoire). Distincte de la marque
	/// du protocole de jeu en clair : les deux formes ne se confondent jamais.
	inline constexpr uint32_t kUdpSecureMagic = 0x4544434Cu;
	/// Type d'enveloppe : poignée de main (Hello).
	inline constexpr uint8_t kUdpSecureTypeHandshake = 1u;
	/// Type d'enveloppe : données.
	inline constexpr uint8_t kUdpSecureTypeData = 2u;

	/// En-tête d'une poignée de main : magic + type + character_id + sel.
	inline constexpr size_t kUdpHandshakeHeaderSize = 4u + 1u + 8u + kUdpSessionSaltSize;
	/// En-tête d'un datagramme de données : magic + type + compteur.
	inline constexpr size_t kUdpDataHeaderSize = 4u + 1u + 8u;
	/// Surcoût d'une poignée de main (en-tête + étiquette) : 45 octets.
	inline constexpr size_t kUdpHandshakeOverhead = kUdpHandshakeHeaderSize + kUdpAeadTagSize;
	/// Surcoût d'un datagramme de données (en-tête + étiquette) : 29 octets.
	inline constexpr size_t kUdpDataOverhead = kUdpDataHeaderSize + kUdpAeadTagSize;

	using UdpSessionKey = std::array<uint8_t, kUdpSessionKeySize>;
	using UdpSessionSalt = std::array<uint8_t, kUdpSessionSaltSize>;

	/// Remplit \p out d'octets aléatoires cryptographiques (OpenSSL RAND_bytes).
	/// \return false si le générateur système est indisponible (ne jamais utiliser
	///         \p out dans ce cas).
	bool FillRandomBytes(std::span<uint8_t> out);

	/// Tire une clé de session neuve. \return false en cas d'échec du générateur.
	bool GenerateUdpSessionKey(UdpSessionKey& outKey);

	/// Vrai si \p datagram commence par la marque d'enveloppe chiffrée (sans rien
	/// vérifier d'autre : l'authentification se fait à l'ouverture).
	bool IsSecureDatagram(std::span<const std::byte> datagram);

	/// Partie en clair d'une poignée de main : identifie la clé à utiliser.
	struct UdpHandshakeHeader
	{
		uint64_t characterId = 0;
		UdpSessionSalt salt{};
	};

	/// Lit l'en-tête en clair d'une poignée de main. Ne prouve RIEN (l'en-tête est
	/// authentifié seulement à l'ouverture). \return false si ce n'est pas une
	/// poignée de main bien formée.
	bool PeekUdpHandshake(std::span<const std::byte> datagram, UdpHandshakeHeader& outHeader);

	/// Fenêtre anti-rejeu : accepte chaque compteur une seule fois, refuse ceux qui
	/// ont plus de 64 positions de retard sur le plus grand compteur accepté.
	class UdpReplayWindow
	{
	public:
		/// Taille de la fenêtre (nombre de compteurs récents mémorisés).
		static constexpr uint64_t kWindowSize = 64u;

		/// Vrai si \p counter n'a jamais été accepté et reste dans la fenêtre.
		/// Ne modifie rien (à appeler avant l'authentification).
		bool CanAccept(uint64_t counter) const;
		/// Marque \p counter comme reçu (à appeler APRÈS une authentification réussie,
		/// sinon un attaquant pourrait faire avancer la fenêtre avec des faux paquets).
		void Mark(uint64_t counter);
		/// Plus grand compteur accepté (0 = aucun).
		uint64_t Highest() const { return m_highest; }

	private:
		uint64_t m_highest = 0;
		uint64_t m_bitmap = 0; ///< bit i = compteur (m_highest - i) déjà reçu.
	};

	/// Rôle d'une extrémité : fixe le sens des nonces émis et attendus.
	enum class UdpSecureRole : uint8_t
	{
		Client, ///< émet en C2S, reçoit en S2C.
		Server, ///< émet en S2C, reçoit en C2S.
	};

	/// Une session chiffrée entre un client et le shard (une par socket UDP client).
	///
	/// Pas thread-safe : l'appelant sérialise Seal/Open (le client appelle tout depuis
	/// la boucle de jeu ; le shard protège ses sessions par un mutex).
	class UdpSecureSession
	{
	public:
		UdpSecureSession() = default;
		~UdpSecureSession();
		UdpSecureSession(const UdpSecureSession&) = delete;
		UdpSecureSession& operator=(const UdpSecureSession&) = delete;
		UdpSecureSession(UdpSecureSession&& other) noexcept;
		UdpSecureSession& operator=(UdpSecureSession&& other) noexcept;

		/// Dérive la clé de travail (HKDF-SHA256) et remet les compteurs à zéro.
		/// \param sessionKey clé de session échangée à l'EnterWorld.
		/// \param salt sel tiré par le client à l'ouverture de sa socket.
		/// \param role côté de cette extrémité.
		/// \return false si la dérivation ou l'initialisation OpenSSL échoue.
		bool Init(const UdpSessionKey& sessionKey, const UdpSessionSalt& salt, UdpSecureRole role);

		/// Vrai après un Init réussi.
		bool IsReady() const { return m_ready; }

		/// Sel de la session (identifie la socket client).
		const UdpSessionSalt& Salt() const { return m_salt; }

		/// Client seulement : scelle le Hello dans une poignée de main (compteur 0).
		/// Une seule fois par session : le compteur 0 ne doit jamais chiffrer deux
		/// contenus (les répétitions renvoient les mêmes octets).
		/// \param characterId personnage annoncé en clair et authentifié.
		/// \return false si la session n'est pas prête, si le rôle n'est pas Client, si
		///         la poignée de main a déjà été scellée, ou en cas d'erreur OpenSSL.
		bool SealHandshake(uint64_t characterId, std::span<const std::byte> plain, std::vector<std::byte>& outDatagram);

		/// Shard seulement : ouvre une poignée de main (compteur 0, sens C2S). Le sel
		/// et le character_id de l'en-tête doivent être ceux de la session.
		/// \return false si l'authentification échoue ou si la poignée de main a déjà
		///         été ouverte sur cette session (rejeu).
		bool OpenHandshake(std::span<const std::byte> datagram, std::vector<std::byte>& outPlain);

		/// Scelle un datagramme de données dans le sens émis par cette extrémité.
		/// \return false si la session n'est pas prête, si le compteur est épuisé, ou en
		///         cas d'erreur OpenSSL.
		bool Seal(std::span<const std::byte> plain, std::vector<std::byte>& outDatagram);

		/// Ouvre un datagramme de données reçu de l'autre extrémité. Refuse les
		/// datagrammes falsifiés, tronqués, rejoués ou trop anciens.
		bool Open(std::span<const std::byte> datagram, std::vector<std::byte>& outPlain);

		/// Nombre de datagrammes scellés (diagnostic).
		uint64_t SealedCount() const { return m_sendCounter == 0u ? 0u : m_sendCounter - 1u; }
		/// Nombre de datagrammes refusés à l'ouverture (diagnostic, logs).
		uint64_t RejectedCount() const { return m_rejected; }

	private:
		void Reset();
		bool SealWith(uint32_t direction, uint64_t counter, std::span<const uint8_t> header,
			std::span<const std::byte> plain, std::vector<std::byte>& out);
		bool OpenWith(uint32_t direction, uint64_t counter, std::span<const uint8_t> header,
			std::span<const std::byte> cipherAndTag, std::vector<std::byte>& outPlain);

		std::array<uint8_t, 32> m_workKey{};
		UdpSessionSalt m_salt{};
		UdpSecureRole m_role = UdpSecureRole::Client;
		bool m_ready = false;
		bool m_handshakeOpened = false;
		bool m_handshakeSealed = false; ///< client : compteur 0 déjà utilisé.
		uint64_t m_sendCounter = 1;  ///< prochain compteur émis (0 réservé à la poignée de main).
		UdpReplayWindow m_receiveWindow;
		uint64_t m_rejected = 0;
		void* m_cipherCtx = nullptr; ///< EVP_CIPHER_CTX* réutilisé (évite une allocation par paquet).
	};
}
