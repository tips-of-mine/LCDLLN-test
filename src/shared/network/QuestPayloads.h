#pragma once
// CMANGOS.23 (Phase 5.23 step 3+4) — Wire payloads pour les opcodes Quest (59–67).
//
// Quatre paires Request/Response + 1 push :
//   - Accept   (59/60)
//   - Complete (61/62)
//   - Reward   (63/64)
//   - List     (65/66)
//   - StateUpdate push (67) — Master→Client pour annoncer un changement d'état
//     déclenché côté serveur (admin reset, expiration, etc.).
//
// Format wire : ByteReader/ByteWriter little-endian. Toutes les payloads sont
// très simples (uint32 questId, uint8 status, vector<{uint32,uint8}> via
// WriteArrayCount/ReadArrayCount). Pas de strings — la quête est référencée
// par son id numérique.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level. Mapping côté serveur depuis QuestOpResult.
	// =========================================================================

	/// Code d'erreur générique pour les opcodes Quest (Accept/Complete/Reward/List).
	/// 0 = OK ; sinon questId / quests peuvent être 0 / vides selon la nature de
	/// l'erreur. Le client expose ces codes en message localisable.
	enum class QuestOpErrorCode : uint8_t
	{
		Ok                = 0,
		WrongStatus       = 1, ///< Mappé depuis QuestOpResult::WrongStatus (transition invalide).
		QuestNotFound     = 2, ///< Mappé depuis QuestOpResult::QuestNotFound.
		Unauthorized      = 6, ///< Pas de session valide côté master.
		NotImplementedYet = 7, ///< Reward V1 : la feature n'est pas câblée à l'inventaire.
	};

	// =========================================================================
	// QUEST_ACCEPT — Client → Master : accepte une quête.
	// =========================================================================

	/// Wire format : uint32 questId (4 octets).
	struct QuestAcceptRequestPayload
	{
		uint32_t questId = 0;
	};

	/// Wire format :
	///   uint8  error      (cf. QuestOpErrorCode)
	///   uint32 questId    (echo de la request)
	///   uint8  newStatus  (cf. quests::QuestStatus). 0 = None si error != Ok.
	struct QuestAcceptResponsePayload
	{
		uint8_t  error     = 0;
		uint32_t questId   = 0;
		uint8_t  newStatus = 0;
	};

	// =========================================================================
	// QUEST_COMPLETE — Client → Master : marque la quête Completed.
	// =========================================================================

	struct QuestCompleteRequestPayload
	{
		uint32_t questId = 0;
	};

	struct QuestCompleteResponsePayload
	{
		uint8_t  error     = 0;
		uint32_t questId   = 0;
		uint8_t  newStatus = 0;
	};

	// =========================================================================
	// QUEST_REWARD — Client → Master : récupère la récompense.
	// =========================================================================

	struct QuestRewardRequestPayload
	{
		uint32_t questId = 0;
	};

	struct QuestRewardResponsePayload
	{
		uint8_t  error     = 0;
		uint32_t questId   = 0;
		uint8_t  newStatus = 0;
	};

	// =========================================================================
	// QUEST_LIST — Client → Master : liste les quêtes connues du compte.
	// =========================================================================

	/// Wire format : (vide). L'account est dérivé de la session côté master.
	struct QuestListRequestPayload
	{
		// (vide)
	};

	/// Une entrée de l'inventaire de quêtes du compte.
	struct QuestStateEntry
	{
		uint32_t questId = 0;
		uint8_t  status  = 0; ///< cf. quests::QuestStatus.
	};

	/// Wire format :
	///   uint8  error
	///   uint16 count          (si error == 0)
	///   <count> entries       (4 + 1 octets chacune)
	struct QuestListResponsePayload
	{
		uint8_t                       error = 0;
		std::vector<QuestStateEntry>  quests;
	};

	// =========================================================================
	// QUEST_STATE_UPDATE — Master → Client (push, request_id=0).
	// Annonce un changement d'état serveur-driven (admin reset, expiration).
	// =========================================================================

	/// Wire format : uint32 questId + uint8 newStatus (5 octets).
	struct QuestStateUpdatePayload
	{
		uint32_t questId   = 0;
		uint8_t  newStatus = 0;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un QUEST_ACCEPT_REQUEST. \return nullopt si payloadSize < 4.
	std::optional<QuestAcceptRequestPayload>   ParseQuestAcceptRequestPayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<QuestCompleteRequestPayload> ParseQuestCompleteRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::optional<QuestRewardRequestPayload>   ParseQuestRewardRequestPayload  (const uint8_t* payload, size_t payloadSize);
	/// Parse le payload d'un QUEST_LIST_REQUEST. Toujours OK (payload vide accepté).
	std::optional<QuestListRequestPayload>     ParseQuestListRequestPayload    (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildQuestAcceptRequestPayload  (uint32_t questId);
	std::vector<uint8_t> BuildQuestCompleteRequestPayload(uint32_t questId);
	std::vector<uint8_t> BuildQuestRewardRequestPayload  (uint32_t questId);
	/// Build payload (vide) d'un QUEST_LIST_REQUEST.
	std::vector<uint8_t> BuildQuestListRequestPayload();

	// -------------------------------------------------------------------------
	// Parse / Build — Responses (payload-only)
	// -------------------------------------------------------------------------

	std::optional<QuestAcceptResponsePayload>   ParseQuestAcceptResponsePayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<QuestCompleteResponsePayload> ParseQuestCompleteResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<QuestRewardResponsePayload>   ParseQuestRewardResponsePayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<QuestListResponsePayload>     ParseQuestListResponsePayload    (const uint8_t* payload, size_t payloadSize);
	std::optional<QuestStateUpdatePayload>      ParseQuestStateUpdatePayload     (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildQuestAcceptResponsePayload  (uint8_t error, uint32_t questId, uint8_t newStatus);
	std::vector<uint8_t> BuildQuestCompleteResponsePayload(uint8_t error, uint32_t questId, uint8_t newStatus);
	std::vector<uint8_t> BuildQuestRewardResponsePayload  (uint8_t error, uint32_t questId, uint8_t newStatus);
	std::vector<uint8_t> BuildQuestListResponsePayload    (uint8_t error, const std::vector<QuestStateEntry>& quests);
	std::vector<uint8_t> BuildQuestStateUpdatePayload     (uint32_t questId, uint8_t newStatus);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilisé côté handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildQuestAcceptResponsePacket  (uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildQuestCompleteResponsePacket(uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildQuestRewardResponsePacket  (uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildQuestListResponsePacket    (uint8_t error, const std::vector<QuestStateEntry>& quests,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);
	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildQuestStateUpdatePacket     (uint32_t questId, uint8_t newStatus,
	                                                      uint64_t sessionIdHeader);
}
