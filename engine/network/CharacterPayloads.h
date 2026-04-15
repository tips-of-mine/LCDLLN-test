#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// Character creation request payload (M39.1).
	/// Carries name, race, class and the five customisation options selected on
	/// the creation screen.
	struct CharacterCreateRequestPayload
	{
		std::string name;

		// M39.1 — extended fields (race, class, customisation).
		uint32_t raceId         = 0; ///< Selected race id (from character_creation/races.json).
		uint32_t classId        = 0; ///< Selected class id (from character_creation/classes.json).
		uint8_t  faceType       = 0; ///< Face preset index [0, 3].
		uint8_t  hairStyle      = 0; ///< Hair style index  [0, 7].
		uint8_t  skinColorIndex = 0; ///< Skin colour index [0, 5].
		uint8_t  hairColorIndex = 0; ///< Hair colour index [0, 7].
		uint8_t  eyeColorIndex  = 0; ///< Eye colour index  [0, 5].
	};

	struct CharacterCreateResponsePayload
	{
		uint8_t success = 0;
		uint64_t character_id = 0;
	};

	std::optional<CharacterCreateRequestPayload> ParseCharacterCreateRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Build a character creation request payload with name only (legacy – server ignores extra fields).
	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name);

	/// Build a full character creation request payload including race, class and customisation (M39.1).
	std::vector<uint8_t> BuildCharacterCreateRequestPayload(const CharacterCreateRequestPayload& payload);

	std::optional<CharacterCreateResponsePayload> ParseCharacterCreateResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterCreateResponsePacket(uint8_t success, uint64_t characterId, uint32_t requestId, uint64_t sessionIdHeader);
}
