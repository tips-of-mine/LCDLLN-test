#pragma once
// ETAPE3.1 (chantier 4.3) — sessions UDP chiffrées du shard, une par point de
// terminaison client.
//
// Rôle : tout l'état cryptographique côté shard, hors de ServerApp (testable seul,
// cf. udp_session_table_tests) :
//   - ouvre la poignée de main chiffrée d'un client (Hello) avec la clé de session de
//     son personnage et installe la session pour son point de terminaison ;
//   - refuse un sel déjà utilisé avec la même clé : un Hello rejoué recréerait la même
//     clé de travail et les mêmes compteurs côté shard, donc les mêmes nonces ;
//   - ouvre les datagrammes de données reçus et scelle ceux envoyés ;
//   - oublie les sessions sans client et les sels trop vieux.
//
// Thread-safe : un mutex interne. Le shard ouvre depuis le thread de jeu et scelle
// depuis n'importe quel thread qui envoie.

#include "src/shared/network/UdpSecureChannel.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	class UdpSessionTable
	{
	public:
		/// Résultat de l'ouverture d'une poignée de main.
		enum class HandshakeResult : uint8_t
		{
			Opened,     ///< Session installée pour ce point de terminaison, Hello déchiffré.
			Duplicate,  ///< Même Hello déjà reçu de ce point de terminaison : session en place, rien à refaire.
			Replayed,   ///< Sel déjà utilisé avec cette clé depuis un autre point de terminaison : refus.
			AuthFailed, ///< En-tête mal formé, étiquette fausse ou erreur OpenSSL : refus.
		};

		/// Nombre maximal de sels mémorisés par personnage (au-delà, le plus ancien sort).
		static constexpr size_t kMaxSaltsPerCharacter = 64u;

		/// Ouvre la poignée de main \p datagram reçue de \p endpointKey avec la clé de
		/// session \p sessionKey du personnage annoncé (en-tête). En cas de succès, la
		/// session remplace celle que ce point de terminaison avait peut-être déjà.
		/// \param nowMs horloge monotone (ms), pour l'âge des sessions et des sels.
		/// \param outPlainHello paquet Hello déchiffré (rempli si Opened).
		HandshakeResult OpenHandshake(uint64_t endpointKey, const engine::network::UdpSessionKey& sessionKey,
			std::span<const std::byte> datagram, uint64_t nowMs, std::vector<std::byte>& outPlainHello);

		/// Ouvre un datagramme de données de \p endpointKey. \return false si le point de
		/// terminaison n'a pas de session ou si le datagramme est falsifié, rejoué ou
		/// trop ancien.
		bool OpenData(uint64_t endpointKey, std::span<const std::byte> datagram, uint64_t nowMs,
			std::vector<std::byte>& outPlain);

		/// Résultat d'un scellement.
		enum class SealResult : uint8_t
		{
			Sealed,    ///< \p outDatagram contient le datagramme scellé.
			NoSession, ///< Pas de session pour ce point de terminaison (l'appelant décide).
			Failed,    ///< Erreur de scellement (compteur épuisé, OpenSSL).
		};

		/// Scelle \p plain pour \p endpointKey.
		SealResult Seal(uint64_t endpointKey, std::span<const std::byte> plain, std::vector<std::byte>& outDatagram);

		/// Vrai si \p endpointKey a une session chiffrée.
		bool HasSession(uint64_t endpointKey) const;

		/// Personnage authentifié de la session de \p endpointKey, 0 si aucune.
		uint64_t SessionCharacterId(uint64_t endpointKey) const;

		/// Supprime la session de \p endpointKey (déconnexion). No-op si absente.
		void Drop(uint64_t endpointKey);

		/// Supprime les sessions dont aucun client n'utilise le point de terminaison
		/// (\p hasClient faux) et inactives depuis \p idleMs, et les sels non revus
		/// depuis \p saltRetentionMs.
		void Prune(uint64_t nowMs, uint64_t idleMs, uint64_t saltRetentionMs,
			const std::function<bool(uint64_t endpointKey)>& hasClient);

		/// Vide tout (démarrage du shard).
		void Clear();

		/// Nombre de sessions (tests, diagnostic).
		size_t SessionCount() const;

	private:
		struct Session
		{
			engine::network::UdpSecureSession crypto;
			uint64_t characterId = 0;
			uint64_t lastActivityMs = 0;
		};
		struct UsedSalts
		{
			engine::network::UdpSessionKey key{};
			std::vector<engine::network::UdpSessionSalt> salts;
			uint64_t lastSeenMs = 0;
		};

		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Session> m_sessions;     ///< par point de terminaison.
		std::unordered_map<uint64_t, UsedSalts> m_usedSalts;  ///< par personnage.
	};
}
