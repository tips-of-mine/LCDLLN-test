#pragma once
// M100.32 — Wire payloads pour les opcodes Interactive Props (200/201/202).
//   - StateChange    (200) : Client → Master. Le joueur a ouvert/fermé un objet.
//   - StateBroadcast (201) : Master → autres clients. Relai de l'évènement.
//   - StateSync      (202) : Master → client entrant. État complet de la zone.
//
// Le master maintient une `std::unordered_map<uint64,uint8>` par zone (cf.
// InteractiveStateRelay). Il reçoit StateChange, écrit l'état, broadcast à
// tous les autres clients, et envoie StateSync à un client qui se connecte.
// AUCUNE validation gameplay (portée, droit d'ouverture, anti-triche).
//
// Format wire : ByteReader/ByteWriter little-endian. uint64 via WriteU64/ReadU64.
// La liste de sync utilise WriteArrayCount/ReadArrayCount (uint16 count).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// INTERACTIVE_STATE_CHANGE (200) — Client → Master.
	// =========================================================================

	/// Wire format :
	///   uint64 id            (identifiant de l'objet interactif)
	///   uint8  newState      (0 = fermé, 1 = ouvert)
	///   uint64 clientTimeMs  (horloge client à l'évènement, pour latence)
	struct InteractiveStateChangePayload
	{
		uint64_t id           = 0;
		uint8_t  newState     = 0;
		uint64_t clientTimeMs = 0;
	};

	// =========================================================================
	// INTERACTIVE_STATE_BROADCAST (201) — Master → autres clients.
	// =========================================================================

	/// Wire format :
	///   uint64 id
	///   uint8  newState
	///   uint64 serverTimeMs  (horloge serveur au relai, pour compensation latence)
	struct InteractiveStateBroadcastPayload
	{
		uint64_t id           = 0;
		uint8_t  newState     = 0;
		uint64_t serverTimeMs = 0;
	};

	// =========================================================================
	// INTERACTIVE_STATE_SYNC (202) — Master → client entrant (état complet).
	// =========================================================================

	/// Un couple (id, état) dans le set de synchronisation initiale.
	struct InteractiveSyncEntry
	{
		uint64_t id    = 0;
		uint8_t  state = 0;
	};

	/// Wire format :
	///   uint16 count
	///   count × { uint64 id, uint8 state }
	struct InteractiveStateSyncPayload
	{
		std::vector<InteractiveSyncEntry> entries;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — payload nu (sans header protocol_v1).
	// -------------------------------------------------------------------------

	std::optional<InteractiveStateChangePayload>    ParseInteractiveStateChangePayload   (const uint8_t* payload, size_t payloadSize);
	std::optional<InteractiveStateBroadcastPayload> ParseInteractiveStateBroadcastPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<InteractiveStateSyncPayload>      ParseInteractiveStateSyncPayload     (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildInteractiveStateChangePayload   (uint64_t id, uint8_t newState, uint64_t clientTimeMs);
	std::vector<uint8_t> BuildInteractiveStateBroadcastPayload(uint64_t id, uint8_t newState, uint64_t serverTimeMs);
	std::vector<uint8_t> BuildInteractiveStateSyncPayload     (const std::vector<InteractiveSyncEntry>& entries);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilisé côté handler serveur.
	// -------------------------------------------------------------------------

	/// Request client → master (opcode 200). `requestId` libre (réponse non
	/// requise : le master relaie via broadcast).
	std::vector<uint8_t> BuildInteractiveStateChangePacket(uint64_t id, uint8_t newState, uint64_t clientTimeMs,
		uint32_t requestId, uint64_t sessionIdHeader);

	/// Push master → autres clients (opcode 201, request_id=0).
	std::vector<uint8_t> BuildInteractiveStateBroadcastPacket(uint64_t id, uint8_t newState, uint64_t serverTimeMs,
		uint64_t sessionIdHeader);

	/// Push master → client entrant (opcode 202, request_id=0).
	std::vector<uint8_t> BuildInteractiveStateSyncPacket(const std::vector<InteractiveSyncEntry>& entries,
		uint64_t sessionIdHeader);
}
