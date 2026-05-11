#pragma once
// CMANGOS.24 (Phase 3.24 step 3+4) — Wire payloads pour les opcodes Reputation (95-97).
//
// Une paire Request/Response + 1 push :
//   - List               (95/96)
//   - UpdateNotification (97 push) — Master to Client pour annoncer un changement
//     de reputation declenche par un game event (quest reward, kill, etc.).
//
// La reputation est read-only cote client : le serveur seul decide de modifier.
// Pas d'opcode "set reputation" — le client ne peut que lire (List) et reagir
// a la notification push (UpdateNotification).
//
// Format wire : ByteReader/ByteWriter little-endian. Les valeurs int32 sont
// passees via static_cast vers/depuis uint32_t (pas de WriteI32 disponible
// dans ByteWriter — voir WriteEntry/ReadEntry dans le .cpp). Les valeurs
// int8 (standing) passent via WriteBytes/ReadBytes 1 octet.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Reputation.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Reputation.
	/// 0 = OK ; sinon le tableau des entries est vide.
	enum class ReputationErrorCode : uint8_t
	{
		Ok           = 0,
		Unauthorized = 6, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// REPUTATION_LIST — Client to Master : liste les reputations du compte.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct ReputationListRequestPayload
	{
		// (vide)
	};

	/// Une entree de la table de reputation (account, faction).
	struct ReputationEntry
	{
		uint32_t factionId = 0;
		int32_t  value     = 0; ///< [-42000 ; +41999] cmangos.
		int8_t   standing  = 0; ///< cf. ReputationStanding (-6 Hated ... +1 Exalted).
	};

	/// Wire format :
	///   uint8  error            (cf. ReputationErrorCode)
	///   uint16 count             (si error == 0)
	///   <count> entries          (4 + 4 + 1 octets chacune)
	struct ReputationListResponsePayload
	{
		uint8_t                       error = 0;
		std::vector<ReputationEntry>  entries;
	};

	// =========================================================================
	// REPUTATION_UPDATE_NOTIFICATION — Master to Client (push, request_id=0).
	// Annonce qu'un changement de reputation a eu lieu cote serveur.
	// =========================================================================

	/// Wire format :
	///   uint32 factionId   (4)
	///   int32  newValue    (4, cast via uint32)
	///   int8   newStanding (1)
	///   int32  delta       (4, cast via uint32 ; positif = gain, negatif = perte)
	struct ReputationUpdateNotificationPayload
	{
		uint32_t factionId   = 0;
		int32_t  newValue    = 0;
		int8_t   newStanding = 0;
		int32_t  delta       = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un REPUTATION_LIST_REQUEST. Toujours OK (payload vide accepte).
	std::optional<ReputationListRequestPayload> ParseReputationListRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildReputationListRequestPayload();

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<ReputationListResponsePayload>       ParseReputationListResponsePayload      (const uint8_t* payload, size_t payloadSize);
	std::optional<ReputationUpdateNotificationPayload> ParseReputationUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildReputationListResponsePayload      (uint8_t error, const std::vector<ReputationEntry>& entries);
	std::vector<uint8_t> BuildReputationUpdateNotificationPayload(uint32_t factionId, int32_t newValue, int8_t newStanding, int32_t delta);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildReputationListResponsePacket(uint8_t error, const std::vector<ReputationEntry>& entries,
	                                                       uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildReputationUpdateNotificationPacket(uint32_t factionId, int32_t newValue, int8_t newStanding, int32_t delta,
	                                                              uint64_t sessionIdHeader);
}
