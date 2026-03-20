#pragma once

#include <cstdint>
#include <string_view>

namespace engine::net
{
	/// Stable wire ids for \ref TryParseEmoteChatLine (must match server relay payload).
	enum class ChatEmoteWireId : uint8_t
	{
		None = 0,
		Dance = 1,
		Wave = 2,
		Laugh = 3,
		Cry = 4,
		Sit = 5,
		Bow = 6,
		Point = 7,
		Salute = 8
	};

	/// Result of parsing one `/emote` chat line (M29.3).
	struct ParsedEmoteChatLine final
	{
		ChatEmoteWireId emote = ChatEmoteWireId::None;
		/// When true, client should loop until cancelled (e.g. \ref ChatEmoteWireId::Sit).
		bool loop = false;
	};

	/// Returns true when \p text is exactly one known emote token (ASCII, case-insensitive).
	bool TryParseEmoteChatLine(std::string_view text, ParsedEmoteChatLine& outParsed);

	/// Human-readable name for logs / debug HUD.
	const char* ChatEmoteName(ChatEmoteWireId emote);

	/// Playback duration for one-shot emotes (seconds). Looping emotes return 0.
	float ChatEmoteOneShotDurationSeconds(ChatEmoteWireId emote);

	/// True when this emote uses looping posture on the client.
	bool ChatEmoteLoops(ChatEmoteWireId emote);

	/// Encode enum to wire byte (0 when None).
	uint8_t ToWire(ChatEmoteWireId emote);

	/// Decode wire byte; returns false when unknown.
	bool TryFromWire(uint8_t wire, ChatEmoteWireId& outEmote);
}
