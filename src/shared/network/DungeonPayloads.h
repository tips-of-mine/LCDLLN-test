#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// M100.43 — Dungeon Portal System (Phase 11). Client → Master :
	/// déclenche un portail de donjon posé dans `instances/dungeon_portals.bin`.
	/// Le master résout (ou crée) une instance dans la table
	/// `dungeon_instances` (migration 0063) et renvoie un shard endpoint.
	/// Wire format :
	///   uint64 characterId
	///   string dungeonTemplateId (length-prefixed UTF-8, ≤ kMaxDungeonTemplateIdBytes)
	///   uint8  difficulty        (1..kMaxDungeonDifficulty)
	///
	/// Réservé en M100.43, **non câblé** jusqu'à M100.44 — un client qui
	/// l'envoie aujourd'hui reçoit BAD_REQUEST côté master.
	struct EnterDungeonRequestPayload
	{
		uint64_t    characterId       = 0u;
		std::string dungeonTemplateId;
		uint8_t     difficulty        = 1u;
	};

	/// M100.43 — Master → Client : ACK ou erreur sur EnterDungeonRequest.
	/// Wire format :
	///   uint8  success         (0 = error, 1 = ok)
	///   uint64 instanceId      (0 si error)
	///   string shardEndpoint   (vide si error, sinon "host:port")
	///   uint8  errorCode       (kEnterDungeonError* si !success)
	struct EnterDungeonResponsePayload
	{
		bool        success        = false;
		uint64_t    instanceId     = 0u;
		std::string shardEndpoint;
		uint8_t     errorCode      = 0u;
	};

	/// Codes d'erreur côté master pour EnterDungeonResponse.
	inline constexpr uint8_t kEnterDungeonErrorNone               = 0u;
	inline constexpr uint8_t kEnterDungeonErrorTemplateNotFound   = 1u; ///< dungeonTemplateId inconnu (catalog éditeur).
	inline constexpr uint8_t kEnterDungeonErrorInstanceFull       = 2u; ///< Cap d'instances actives atteint.
	inline constexpr uint8_t kEnterDungeonErrorDifficultyLocked   = 3u; ///< Difficulty > niveau perso ou progression manquante.
	inline constexpr uint8_t kEnterDungeonErrorUnauthorized       = 4u; ///< Session invalide / character pas owned.
	inline constexpr uint8_t kEnterDungeonErrorNotYetImplemented  = 5u; ///< M100.43 (réservé) — handler pas encore câblé en M100.44.

	inline constexpr size_t  kMaxDungeonTemplateIdBytes = 64u;
	inline constexpr uint8_t kMaxDungeonDifficulty      = 5u;

	std::vector<uint8_t> BuildEnterDungeonRequestPayload(uint64_t characterId,
		std::string_view dungeonTemplateId, uint8_t difficulty);
	std::optional<EnterDungeonRequestPayload> ParseEnterDungeonRequestPayload(
		const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildEnterDungeonResponsePayload(bool success,
		uint64_t instanceId, std::string_view shardEndpoint, uint8_t errorCode);
	std::optional<EnterDungeonResponsePayload> ParseEnterDungeonResponsePayload(
		const uint8_t* payload, size_t payloadSize);
}
