#pragma once
// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Wire payloads pour les opcodes
// Loot (182-187). 2 paires Request/Response + 2 push notifications :
//   - RollNotification        (182, push, request_id=0) : nouvelle roll proposee.
//   - Choice                  (183/184)                 : Need/Greed/Pass.
//   - RollResultNotification  (185, push, request_id=0) : roll terminee.
//   - SimulateRoll            (186/187)                 : DEBUG simule une roll.
//
// Le master tient en memoire un registry de rolls actifs (V1 : un seul
// eligible par roll, le creator) + items hardcodes (5 entries). Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Strings via
// WriteString/ReadString (uint16 length + UTF-8 bytes), uint8 ecrits via
// WriteBytes(&val, 1u).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur - wire-level pour Loot.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Loot. 0 = OK.
	enum class LootResponseStatus : uint8_t
	{
		Ok            = 0,
		Unauthorized  = 1, ///< Pas de session valide cote master.
		InvalidChoice = 2, ///< choice n'est pas 0/1/2.
		RollNotFound  = 3, ///< rollId inconnu du registry master.
		RollEnded     = 4, ///< Roll deja resolved (timeout ou tous les choix recus).
	};

	// =========================================================================
	// LOOT_ROLL_NOTIFICATION - Master to Client (push, requestId=0).
	// Nouvelle roll proposee : eligible doit choisir Need/Greed/Pass.
	// =========================================================================

	/// Wire format :
	///   uint64 rollId
	///   uint32 itemTemplateId
	///   string itemName
	///   uint32 count
	///   uint32 durationSec
	struct LootRollNotificationPayload
	{
		uint64_t    rollId         = 0;
		uint32_t    itemTemplateId = 0;
		std::string itemName;
		uint32_t    count          = 0;
		uint32_t    durationSec    = 0;
	};

	// =========================================================================
	// LOOT_ROLL_CHOICE - Client to Master : choix Need/Greed/Pass.
	// =========================================================================

	/// Wire format :
	///   uint64 rollId
	///   uint8  choice (0=Pass, 1=Greed, 2=Need)
	struct LootRollChoiceRequestPayload
	{
		uint64_t rollId = 0;
		uint8_t  choice = 0;
	};

	/// Wire format :
	///   uint8 status (cf. LootResponseStatus)
	struct LootRollChoiceResponsePayload
	{
		uint8_t status = 0;
	};

	// =========================================================================
	// LOOT_ROLL_RESULT_NOTIFICATION - Master to Client (push, requestId=0).
	// Roll terminee : winner connu (ou personne si tous Pass).
	// =========================================================================

	/// Wire format :
	///   uint64 rollId
	///   string winnerName        (vide si personne, tous Pass)
	///   uint8  winnerChoice      (0=Pass/1=Greed/2=Need)
	///   uint8  winnerRoll        (0..100, 0 si tous Pass)
	///   uint32 itemTemplateId
	///   string itemName
	///   uint32 count
	struct LootRollResultNotificationPayload
	{
		uint64_t    rollId         = 0;
		std::string winnerName;
		uint8_t     winnerChoice   = 0;
		uint8_t     winnerRoll     = 0;
		uint32_t    itemTemplateId = 0;
		std::string itemName;
		uint32_t    count          = 0;
	};

	// =========================================================================
	// LOOT_SIMULATE_ROLL - Client to Master : DEBUG simule une roll.
	// =========================================================================

	/// Wire format : (vide). V1 outil dev, le creator devient seul eligible.
	struct LootSimulateRollRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  status (cf. LootResponseStatus)
	///   uint64 rollId   (si status == 0, sinon 0)
	struct LootSimulateRollResponsePayload
	{
		uint8_t  status = 0;
		uint64_t rollId = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build - Notifications & Requests (payload-only)
	// -------------------------------------------------------------------------

	std::optional<LootRollNotificationPayload>        ParseLootRollNotificationPayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<LootRollChoiceRequestPayload>       ParseLootRollChoiceRequestPayload      (const uint8_t* payload, size_t payloadSize);
	std::optional<LootRollChoiceResponsePayload>      ParseLootRollChoiceResponsePayload     (const uint8_t* payload, size_t payloadSize);
	std::optional<LootRollResultNotificationPayload>  ParseLootRollResultNotificationPayload (const uint8_t* payload, size_t payloadSize);
	std::optional<LootSimulateRollRequestPayload>     ParseLootSimulateRollRequestPayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<LootSimulateRollResponsePayload>    ParseLootSimulateRollResponsePayload   (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildLootRollNotificationPayload      (uint64_t rollId, uint32_t itemTemplateId, const std::string& itemName,
	                                                            uint32_t count, uint32_t durationSec);
	std::vector<uint8_t> BuildLootRollChoiceRequestPayload     (uint64_t rollId, uint8_t choice);
	std::vector<uint8_t> BuildLootRollChoiceResponsePayload    (uint8_t status);
	std::vector<uint8_t> BuildLootRollResultNotificationPayload(uint64_t rollId, const std::string& winnerName,
	                                                            uint8_t winnerChoice, uint8_t winnerRoll,
	                                                            uint32_t itemTemplateId, const std::string& itemName,
	                                                            uint32_t count);
	std::vector<uint8_t> BuildLootSimulateRollRequestPayload   ();
	std::vector<uint8_t> BuildLootSimulateRollResponsePayload  (uint8_t status, uint64_t rollId);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildLootRollNotificationPacket       (uint64_t rollId, uint32_t itemTemplateId, const std::string& itemName,
	                                                            uint32_t count, uint32_t durationSec, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildLootRollChoiceResponsePacket     (uint8_t status, uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildLootRollResultNotificationPacket (uint64_t rollId, const std::string& winnerName,
	                                                            uint8_t winnerChoice, uint8_t winnerRoll,
	                                                            uint32_t itemTemplateId, const std::string& itemName,
	                                                            uint32_t count, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildLootSimulateRollResponsePacket   (uint8_t status, uint64_t rollId,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
}
