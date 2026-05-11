// Paquet ERROR du protocole v1 : format binaire, construction côté serveur, parsing côté client.
// Format du payload ERROR : [ uint32 error_code ][ uint16 message_len ][ message_utf8 (message_len octets) ]
// Le message est optionnel (message_len peut être 0). Le request_id est dans l'en-tête du paquet.
// Thread-safety : les fonctions stateless sont thread-safe ; aucun état partagé.
#pragma once

#include "src/shared/network/NetErrorCode.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Payload ERROR désérialisé. Le request_id associé se trouve dans l'en-tête du paquet.
	struct ErrorPayload
	{
		NetErrorCode errorCode = NetErrorCode::OK; ///< Code d'erreur réseau (voir NetErrorCode.h).
		std::string message;                        ///< Message lisible facultatif (peut être vide).
	};

	/// Construit un paquet ERROR complet (en-tête v1 + payload) prêt à l'envoi.
	/// Utilisé par le serveur pour signaler une erreur en réponse à une requête client.
	/// @param errorCode   Code d'erreur à inclure dans le payload.
	/// @param message     Message lisible optionnel (éviter les détails internes en production).
	///                    Tronqué silencieusement à kProtocolV1MaxStringLength si trop long.
	/// @param requestId   request_id de l'en-tête : doit correspondre à la requête cliente (0 si aucune).
	/// @param sessionId   session_id de l'en-tête (0 si non authentifié).
	/// @return Paquet sérialisé prêt à l'envoi, ou vecteur vide si la sérialisation échoue.
	std::vector<uint8_t> BuildErrorPacket(NetErrorCode errorCode, std::string_view message,
		uint32_t requestId, uint64_t sessionId);

	/// Désérialise le payload d'un paquet ERROR reçu.
	/// Format attendu : [ uint32 error_code ][ uint16 message_len ][ message_utf8 ].
	/// Le message est optionnel : si payloadSize == 4, message reste vide.
	/// @param payload      Pointeur sur le début du payload (après l'en-tête paquet).
	/// @param payloadSize  Taille du payload en octets.
	/// @return ErrorPayload parsé, ou std::nullopt si le buffer est tronqué ou invalide (< 4 octets).
	std::optional<ErrorPayload> ParseErrorPayload(const uint8_t* payload, size_t payloadSize);
}
