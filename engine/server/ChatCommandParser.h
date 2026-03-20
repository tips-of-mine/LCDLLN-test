#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Known slash commands for M29.2 chat moderation (distinct from M29.1 channel prefixes).
	enum class ChatSlashCommandKind : uint8_t
	{
		None = 0,
		Who,
		Ignore,
		Unignore,
		Report,
		Kick,
		Ban,
		Mute,
		Announce
	};

	/// Result of parsing one `/command ...` line from chat input.
	struct ParsedChatSlashCommand final
	{
		ChatSlashCommandKind kind = ChatSlashCommandKind::None;
		/// Text after the command token (arguments); may be empty.
		std::string argsRemainder;
	};

	/// Returns true when \p text starts with a known moderation/chat command token.
	bool TryParseChatSlashCommand(std::string_view text, ParsedChatSlashCommand& outCommand);

	/// Stable label for logs (includes leading slash).
	const char* ChatSlashCommandLabel(ChatSlashCommandKind kind);

	/// ASCII case-insensitive equality for chat names like `P12`.
	bool ChatNameEqualsAsciiI(std::string_view a, std::string_view b);
}
