#pragma once

/// @file ShardWireAuth.h
/// @brief Authentification HMAC-SHA256 du canal shard↔master (audit F3).
///
/// SHARD_REGISTER et SHARD_HEARTBEAT (opcodes 10/13) transitent sur le même
/// port TCP public que les connexions joueur, sans authentification propre
/// avant cet ajout : n'importe quel pair pouvait forger un enregistrement de
/// shard ou un heartbeat. On préfixe désormais chaque payload d'un tag
/// HMAC-SHA256(secret, body) de 32 octets, calculé côté shard à l'émission
/// et vérifié côté master avant tout parsing. WIRE-BREAKING : nécessite un
/// déploiement lock-step master + shard partageant `shard.ticket_hmac_secret`.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::network
{
	/// Taille du tag d'authentification HMAC-SHA256 préfixé aux payloads shard↔master.
	constexpr size_t kShardAuthTagSize = 32u;

	/// Préfixe un tag HMAC-SHA256(secret, body) de 32 octets à \a body.
	/// Renvoie tag||body, ou {} si \a secret est vide ou en cas d'erreur OpenSSL.
	std::vector<uint8_t> WrapShardAuth(std::string_view secret, const std::vector<uint8_t>& body);

	/// Vérifie le tag préfixe (comparaison à temps constant) sur les octets restants.
	/// Renvoie {pointeur_body, taille_body} après le tag, ou nullopt si trop court,
	/// tag invalide, ou secret vide.
	std::optional<std::pair<const uint8_t*, size_t>> UnwrapShardAuth(
		std::string_view secret, const uint8_t* payload, size_t size);
}
