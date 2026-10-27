#pragma once
// ETAPE3.1 (chantier 4.3) — sessions UDP chiffrées du shard, une par point de
// terminaison client.
//
// Rôle : tout l'état cryptographique des sessions côté shard, hors de ServerApp
// (testable seul, cf. udp_session_table_tests) :
//   - ouvre la poignée de main chiffrée d'un client (Hello) avec la clé de session de
//     son personnage et installe la session pour son point de terminaison ;
//   - refuse qu'un point de terminaison déjà lié à un personnage soit repris par un
//     autre (adresse source usurpée par un joueur qui a sa propre clé) ;
//   - délègue l'anti-rejeu du sel à l'appelant (AdmittedCharacterRegistry, où les sels
//     vivent avec la clé) ;
//   - ouvre les datagrammes de données reçus et scelle ceux envoyés ;
//   - oublie les sessions sans client.
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
			Opened,       ///< Session installée pour ce point de terminaison, Hello déchiffré.
			Duplicate,    ///< Même Hello déjà reçu de ce point de terminaison : session en place.
			Replayed,     ///< Sel déjà utilisé avec cette clé (consumeSalt a refusé) : refus.
			EndpointBusy, ///< Le point de terminaison a une session pour un AUTRE personnage : refus.
			AuthFailed,   ///< En-tête mal formé, étiquette fausse ou erreur OpenSSL : refus.
		};

		/// Consomme un sel (vrai = jamais vu avec cette clé, désormais marqué). Appelé
		/// seulement après une authentification réussie, sous le verrou de la table.
		using SaltConsumer = std::function<bool(const engine::network::UdpSessionSalt& salt)>;

		/// Ouvre la poignée de main \p datagram reçue de \p endpointKey avec la clé de
		/// session \p sessionKey du personnage annoncé (en-tête). En cas de succès, la
		/// session remplace celle que ce point de terminaison avait peut-être pour le
		/// MÊME personnage (nouvelle socket du même joueur).
		/// \param nowMs horloge monotone (ms), pour l'âge des sessions.
		/// \param consumeSalt anti-rejeu du sel (cf. SaltConsumer). Un faux Hello (échec
		///        d'authentification) ne consomme rien.
		/// \param outPlainHello paquet Hello déchiffré (rempli si Opened).
		HandshakeResult OpenHandshake(uint64_t endpointKey, const engine::network::UdpSessionKey& sessionKey,
			std::span<const std::byte> datagram, uint64_t nowMs, const SaltConsumer& consumeSalt,
			std::vector<std::byte>& outPlainHello);

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
		/// (\p inUse faux) et inactives depuis \p idleMs.
		void Prune(uint64_t nowMs, uint64_t idleMs, const std::function<bool(uint64_t endpointKey)>& inUse);

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
		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Session> m_sessions; ///< par point de terminaison.
	};
}
