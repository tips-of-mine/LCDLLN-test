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
		Announce,
		/// M32.1 — Friend system commands: /friend add|accept|decline|remove <name>
		Friend,
		/// M32.2 — Party invite command: /invite <name>
		Invite,
		/// M32.2 — Voluntary party leave: /leave
		Leave,
		/// M32.2 — Party loot mode change (leader only): /loot <mode>
		Loot,
		/// M32.2 — Party kick (leader only): /pkick <name>
		PartyKick
	};

	/// Sub-command for ChatSlashCommandKind::Friend (M32.1).
	enum class FriendSubCommand : uint8_t
	{
		Unknown = 0,
		Add,
		Accept,
		Decline,
		Remove
	};

	/// Parse a friend sub-command and extract the target name from argsRemainder.
	/// Returns FriendSubCommand::Unknown when argsRemainder does not match.
	FriendSubCommand ParseFriendSubCommand(std::string_view argsRemainder, std::string& outTargetName);

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

	// -------------------------------------------------------------------------
	// M32.2 — Party command helpers
	// -------------------------------------------------------------------------

	/// Parse a loot mode token from /loot argsRemainder.
	/// Returns the raw token (lowercase) in \p outToken.
	/// Returns false when argsRemainder is empty.
	bool ParseLootCommandToken(std::string_view argsRemainder, std::string& outToken);

	/// Parse a target name from /invite or /pkick argsRemainder.
	/// Returns false when argsRemainder is empty after trimming.
	bool ParsePartyTargetName(std::string_view argsRemainder, std::string& outTargetName);
}
