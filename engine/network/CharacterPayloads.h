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
	/// Phase 3.6 — spawn position (5 floats) appended at the end of the wire entry
	/// to keep the wire format extension forward-compatible (older clients can ignore
	/// extra bytes if the parse loop is tolerant — current parser is strict, so server
	/// + client must deploy together).
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
		// Phase 3.6 — spawn (mètres / degrés). Defaults sensibles si la DB renvoie 0
		// (ex. row pré-migration) : spawn_y=100 plutôt que 0 pour ne pas spawner sous le terrain.
		float    spawn_x          = 0.0f;
		float    spawn_y          = 100.0f;
		float    spawn_z          = 0.0f;
		float    spawn_yaw_deg    = 0.0f;
		float    spawn_pitch_deg  = -10.0f;
		// Phase 3.8 — race/class string identifiers (cf. game/data/races/{races,classes}.json).
		// Vides si character créé pré-migration 0033, ou si l'utilisateur n'a pas choisi.
		std::string race_str;
		std::string class_str;
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

	/// Phase 3.9 — Character delete request payload (8 bytes).
	struct CharacterDeleteRequestPayload
	{
		uint64_t characterId = 0;
	};

	struct CharacterDeleteResponsePayload
	{
		uint8_t success = 0; ///< 1 = soft-deleted ; 0 = error (NOT_FOUND, NOT_OWNED, etc.).
	};

	std::optional<CharacterDeleteRequestPayload> ParseCharacterDeleteRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterDeleteRequestPayload(uint64_t characterId);

	std::optional<CharacterDeleteResponsePayload> ParseCharacterDeleteResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterDeleteResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);

	/// Phase 3.6.5 — Save current character position to master (persisted in characters.spawn_*).
	/// Wire format : uint64 character_id + 5 × float (x, y, z, yaw_deg, pitch_deg) — 28 bytes total.
	struct CharacterSavePositionRequestPayload
	{
		uint64_t characterId   = 0;
		float    x             = 0.0f;
		float    y             = 0.0f;
		float    z             = 0.0f;
		float    yawDeg        = 0.0f;
		float    pitchDeg      = 0.0f;
	};

	struct CharacterSavePositionResponsePayload
	{
		uint8_t success = 0; ///< 1 = saved ; 0 = error (NOT_FOUND, NOT_OWNED, INTERNAL).
	};

	std::optional<CharacterSavePositionRequestPayload> ParseCharacterSavePositionRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterSavePositionRequestPayload(uint64_t characterId, float x, float y, float z, float yawDeg, float pitchDeg);

	std::optional<CharacterSavePositionResponsePayload> ParseCharacterSavePositionResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterSavePositionResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);
}
