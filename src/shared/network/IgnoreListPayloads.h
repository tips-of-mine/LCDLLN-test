#pragma once
// CMANGOS.25 (Phase 3.25 step 3+4) — Wire payloads pour les opcodes IgnoreList
// (68–73).
//
// Trois paires Request/Response :
//   - Add (68/69)
//   - Remove (70/71)
//   - List (72/73)
//
// Format wire : ByteReader/ByteWriter little-endian. Pour le List response,
// le vector<uint64> est encodé via WriteArrayCount (uint16) puis count fois
// uint64 (compatible MailListInbox).
//
// Le step 1 (IgnoreListManager + IIgnoreStore + InMemoryIgnoreStore) et le
// step 2 (MysqlIgnoreStore + migration 0049) sont déjà mergés. Cette PR
// ajoute le wire et l'UI client.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Code d'erreur commun aux 3 réponses Ignore (Add / Remove / List).
	// La cardinalité est petite et stable ; on garde un seul enum.
	// =========================================================================

	/// Codes d'erreur retournés dans les *ResponsePayload IgnoreList.
	/// Aligné sur engine::server::social::IgnoreOpResult (cf. IgnoreList.h)
	/// avec en plus l'erreur Unauthorized propre au wire (session manquante).
	enum class IgnoreOpErrorCode : uint8_t
	{
		Ok              = 0,
		AlreadyIgnored  = 1, ///< Cas Add : l'account est déjà dans la liste.
		NotIgnored      = 2, ///< Cas Remove : l'account n'est pas dans la liste.
		ListFull        = 3, ///< Cap kMaxIgnoredPerAccount (50) atteint.
		SelfIgnore      = 4, ///< Pas le droit de s'ignorer soi-même.
		Unauthorized    = 6, ///< Pas de session valide.
	};

	// =========================================================================
	// IGNORE_ADD — Client → Master : ajoute un account à la liste d'ignore.
	// =========================================================================

	/// Wire format : uint64 targetAccountId (8 octets).
	struct IgnoreAddRequestPayload
	{
		uint64_t targetAccountId = 0;
	};

	/// Wire format :
	///   uint8  error (cf. IgnoreOpErrorCode)
	///   uint64 targetAccountId  (echo de la request, pour correlation côté UI)
	struct IgnoreAddResponsePayload
	{
		uint8_t  error           = 0;
		uint64_t targetAccountId = 0;
	};

	// =========================================================================
	// IGNORE_REMOVE — Client → Master : retire un account de la liste d'ignore.
	// =========================================================================

	/// Wire format : uint64 targetAccountId (8 octets).
	struct IgnoreRemoveRequestPayload
	{
		uint64_t targetAccountId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 targetAccountId
	struct IgnoreRemoveResponsePayload
	{
		uint8_t  error           = 0;
		uint64_t targetAccountId = 0;
	};

	// =========================================================================
	// IGNORE_LIST — Client → Master : demande la liste complète des account_id
	// ignorés. L'account propriétaire est dérivé de la session côté master.
	// =========================================================================

	/// Wire format : (vide).
	struct IgnoreListRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error
	///   uint16 count
	///   <count> * uint64 ignoredAccountId
	struct IgnoreListResponsePayload
	{
		uint8_t                 error = 0;
		std::vector<uint64_t>   ignoredAccountIds;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un IGNORE_ADD_REQUEST. \return nullopt si payloadSize < 8.
	std::optional<IgnoreAddRequestPayload> ParseIgnoreAddRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un IGNORE_REMOVE_REQUEST. \return nullopt si payloadSize < 8.
	std::optional<IgnoreRemoveRequestPayload> ParseIgnoreRemoveRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un IGNORE_LIST_REQUEST. Toujours OK (payload vide accepté).
	std::optional<IgnoreListRequestPayload> ParseIgnoreListRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Construit le payload (sans header) d'un IGNORE_ADD_REQUEST.
	std::vector<uint8_t> BuildIgnoreAddRequestPayload(uint64_t targetAccountId);

	/// Construit le payload (sans header) d'un IGNORE_REMOVE_REQUEST.
	std::vector<uint8_t> BuildIgnoreRemoveRequestPayload(uint64_t targetAccountId);

	/// Construit le payload (vide) d'un IGNORE_LIST_REQUEST.
	std::vector<uint8_t> BuildIgnoreListRequestPayload();

	// -------------------------------------------------------------------------
	// Parse / Build — Responses
	// -------------------------------------------------------------------------

	std::optional<IgnoreAddResponsePayload>     ParseIgnoreAddResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<IgnoreRemoveResponsePayload>  ParseIgnoreRemoveResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<IgnoreListResponsePayload>    ParseIgnoreListResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Construit un paquet complet IGNORE_ADD_RESPONSE prêt à envoyer.
	std::vector<uint8_t> BuildIgnoreAddResponsePacket(uint8_t error, uint64_t targetAccountId,
	                                                  uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet IGNORE_REMOVE_RESPONSE.
	std::vector<uint8_t> BuildIgnoreRemoveResponsePacket(uint8_t error, uint64_t targetAccountId,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet IGNORE_LIST_RESPONSE.
	std::vector<uint8_t> BuildIgnoreListResponsePacket(uint8_t error,
	                                                    const std::vector<uint64_t>& ignoredAccountIds,
	                                                    uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// Build payload-only (utile côté tests round-trip).
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildIgnoreAddResponsePayload(uint8_t error, uint64_t targetAccountId);
	std::vector<uint8_t> BuildIgnoreRemoveResponsePayload(uint8_t error, uint64_t targetAccountId);
	std::vector<uint8_t> BuildIgnoreListResponsePayload(uint8_t error, const std::vector<uint64_t>& ignoredAccountIds);
}
