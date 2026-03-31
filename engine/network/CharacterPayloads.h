#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	struct CharacterCreateRequestPayload
	{
		std::string name;
	};

	struct CharacterCreateResponsePayload
	{
		uint8_t success = 0;
		uint64_t character_id = 0;
	};

	std::optional<CharacterCreateRequestPayload> ParseCharacterCreateRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterCreateRequestPayload(std::string_view name);

	std::optional<CharacterCreateResponsePayload> ParseCharacterCreateResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildCharacterCreateResponsePacket(uint8_t success, uint64_t characterId, uint32_t requestId, uint64_t sessionIdHeader);
}
