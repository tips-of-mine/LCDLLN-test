#pragma once
// CMANGOS.32 (Phase 5.32 step 3+4) — Wire payloads pour les opcodes GmTickets (76-82).
//
// Trois paires Request/Response + 1 push :
//   - Open      (76/77)
//   - ListMine  (78/79)
//   - Cancel    (80/81)
//   - ResolvedNotification (82) — Master->Client push asynchrone (request_id=0)
//     pour annoncer qu'un GM a resolu un ticket de ce joueur.
//
// Format wire : ByteReader/ByteWriter little-endian, strings length-prefixed
// UTF-8 (uint16 length puis octets bruts), vectors prefixes par un uint16 count
// via WriteArrayCount/ReadArrayCount (cf. ProtocolV1).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level. Mapping cote serveur depuis l'API
	// GmTicketSystem (Open / Cancel) + checks a l'arrivee.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes GmTickets.
	/// 0 = OK ; sinon ticketId == 0 ou tableau vide selon la nature de l'erreur.
	enum class GmTicketErrorCode : uint8_t
	{
		Ok               = 0,
		BodyEmpty        = 1,  ///< body est vide ou ne contient que du whitespace.
		BodyTooLong      = 2,  ///< body > kMaxGmTicketBodyBytes.
		NotFound         = 3,  ///< ticketId inconnu cote serveur.
		NotOwner         = 4,  ///< le joueur tente de cancel un ticket pas le sien.
		AlreadyResolved  = 5,  ///< tentative de cancel sur un ticket Resolved (pas annulable).
		Unauthorized     = 6,  ///< pas de session valide cote master.
	};

	/// Plafond canonique du body d'un ticket. Au-dela, le serveur retourne
	/// BodyTooLong et le client doit tronquer ou refuser cote UI.
	inline constexpr size_t kMaxGmTicketBodyBytes = 4096u;

	// =========================================================================
	// GMTICKET_OPEN — Client -> Master : ouvre un nouveau ticket support.
	// =========================================================================

	/// Wire format : string body.
	struct GmTicketOpenRequestPayload
	{
		std::string body;
	};

	/// Wire format :
	///   uint8  error      (cf. GmTicketErrorCode)
	///   uint64 ticketId   (0 si error != Ok)
	struct GmTicketOpenResponsePayload
	{
		uint8_t  error    = 0;
		uint64_t ticketId = 0;
	};

	// =========================================================================
	// GMTICKET_LIST_MINE — Client -> Master : liste les tickets de ce joueur.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct GmTicketListMineRequestPayload
	{
		// (vide)
	};

	/// Une entree resumee de la liste de tickets ouverts par le joueur.
	/// Le body n'est pas renvoye (economise la bande passante) — V1 affiche
	/// uniquement etat + horodatage cote UI joueur. Si besoin futur d'afficher
	/// le body relu, ajouter une opcode GmTicketReadRequest dediee.
	struct GmTicketEntry
	{
		uint64_t id            = 0;
		uint64_t createdTsMs   = 0;
		uint64_t resolvedTsMs  = 0;
		uint8_t  state         = 0; ///< 0=Open, 1=Assigned, 2=Resolved, 3=Cancelled.
	};

	/// Wire format :
	///   uint8  error
	///   uint16 count          (si error == 0)
	///   <count> entries       (8 + 8 + 8 + 1 = 25 octets chacune)
	struct GmTicketListMineResponsePayload
	{
		uint8_t                     error = 0;
		std::vector<GmTicketEntry>  tickets;
	};

	// =========================================================================
	// GMTICKET_CANCEL — Client -> Master : annule son propre ticket.
	// =========================================================================

	/// Wire format : uint64 ticketId.
	struct GmTicketCancelRequestPayload
	{
		uint64_t ticketId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 ticketId  (echo de la request)
	struct GmTicketCancelResponsePayload
	{
		uint8_t  error    = 0;
		uint64_t ticketId = 0;
	};

	// =========================================================================
	// GMTICKET_RESOLVED_NOTIFICATION — Master -> Client (push, request_id=0).
	// Annonce qu'un GM a resolu un ticket de ce joueur (api admin server-side).
	// =========================================================================

	/// Wire format : uint64 ticketId + uint64 resolvedTsMs (16 octets).
	struct GmTicketResolvedNotificationPayload
	{
		uint64_t ticketId     = 0;
		uint64_t resolvedTsMs = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un GMTICKET_OPEN_REQUEST.
	/// \return nullopt si le payload est tronque ou malforme.
	std::optional<GmTicketOpenRequestPayload>     ParseGmTicketOpenRequestPayload    (const uint8_t* payload, size_t payloadSize);
	/// Parse le payload d'un GMTICKET_LIST_MINE_REQUEST. Toujours OK (payload vide accepte).
	std::optional<GmTicketListMineRequestPayload> ParseGmTicketListMineRequestPayload(const uint8_t* payload, size_t payloadSize);
	/// Parse le payload d'un GMTICKET_CANCEL_REQUEST. \return nullopt si payloadSize < 8.
	std::optional<GmTicketCancelRequestPayload>   ParseGmTicketCancelRequestPayload  (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGmTicketOpenRequestPayload    (std::string_view body);
	std::vector<uint8_t> BuildGmTicketListMineRequestPayload();
	std::vector<uint8_t> BuildGmTicketCancelRequestPayload  (uint64_t ticketId);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<GmTicketOpenResponsePayload>          ParseGmTicketOpenResponsePayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<GmTicketListMineResponsePayload>      ParseGmTicketListMineResponsePayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<GmTicketCancelResponsePayload>        ParseGmTicketCancelResponsePayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<GmTicketResolvedNotificationPayload>  ParseGmTicketResolvedNotificationPayload (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGmTicketOpenResponsePayload         (uint8_t error, uint64_t ticketId);
	std::vector<uint8_t> BuildGmTicketListMineResponsePayload     (uint8_t error, const std::vector<GmTicketEntry>& tickets);
	std::vector<uint8_t> BuildGmTicketCancelResponsePayload       (uint8_t error, uint64_t ticketId);
	std::vector<uint8_t> BuildGmTicketResolvedNotificationPayload (uint64_t ticketId, uint64_t resolvedTsMs);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildGmTicketOpenResponsePacket     (uint8_t error, uint64_t ticketId,
	                                                          uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGmTicketListMineResponsePacket (uint8_t error, const std::vector<GmTicketEntry>& tickets,
	                                                          uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGmTicketCancelResponsePacket   (uint8_t error, uint64_t ticketId,
	                                                          uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildGmTicketResolvedNotificationPacket(uint64_t ticketId, uint64_t resolvedTsMs,
	                                                             uint64_t sessionIdHeader);
}
