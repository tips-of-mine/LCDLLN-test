#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// M39.1 — Customization options chosen by the player during character creation.
	struct CharacterCustomization
	{
		uint8_t faceType     = 0; ///< Face type index [0, N).
		uint8_t hairStyle    = 0; ///< Hair style index [0, N).
		uint8_t skinColorIdx = 0; ///< Skin colour palette index [0, N).
		uint8_t hairColorIdx = 0; ///< Hair colour palette index [0, N).
		uint8_t eyeColorIdx  = 0; ///< Eye colour palette index [0, N).
	};

	/// M39.1 — Extended character-create request payload (name + race + class + customization).
	/// Wire format (little-endian, null-terminated strings):
	///   uint8  nameLen  + nameLen bytes   (name)
	///   uint8  raceLen  + raceLen bytes   (raceId)
	///   uint8  classLen + classLen bytes  (classId)
	///   5 × uint8                         (customization fields)
	struct CharacterCreateRequestPayload
	{
		std::string           name;
		std::string           raceId;    ///< M39.1 — race identifier (e.g. "humains").
		std::string           classId;   ///< M39.1 — class identifier (e.g. "warrior").
		CharacterCustomization customization{}; ///< M39.1 — appearance options.
	};

	struct CharacterCreateResponsePayload
	{
		uint8_t success = 0;
		uint64_t character_id = 0;
	};

	std::optional<CharacterCreateRequestPayload> ParseCharacterCreateRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name,
	                                                        std::string_view raceId  = {},
	                                                        std::string_view classId = {},
	                                                        const CharacterCustomization& customization = {});

	std::optional<CharacterCreateResponsePayload> ParseCharacterCreateResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterCreateResponsePacket(uint8_t success, uint64_t characterId, uint32_t requestId, uint64_t sessionIdHeader);

	/// Phase 1 — Character list request payload.
	/// Wire format : uint32 serverId (the server_id of the shard the user just selected).
	struct CharacterListRequestPayload
	{
		uint32_t serverId = 0;
	};

	/// Phase 1 — One entry of the character list response.
	/// Mirrors the columns the client needs to render CharacterSelect or to decide
	/// CharacterSelect (>=1 entry) vs CharacterCreate (0 entry).
	struct CharacterListEntry
	{
		uint64_t character_id     = 0;
		uint8_t  slot             = 0;
		std::string name;
		uint32_t race_id          = 0;
		uint16_t class_id         = 0;
		uint16_t level            = 1;
		uint8_t  force_rename     = 0;
		uint64_t last_seen_unix   = 0; ///< Unix timestamp seconds; 0 if character never logged in.
		uint64_t total_play_secs  = 0;
	};

	struct CharacterListResponsePayload
	{
		uint8_t success = 0; ///< 0 = error (count ignored), 1 = OK.
		std::vector<CharacterListEntry> entries;
	};

	std::optional<CharacterListRequestPayload> ParseCharacterListRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterListRequestPayload(uint32_t serverId);

	std::optional<CharacterListResponsePayload> ParseCharacterListResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterListResponsePacket(uint8_t success, const std::vector<CharacterListEntry>& entries,
	                                                     uint32_t requestId, uint64_t sessionIdHeader);
}
