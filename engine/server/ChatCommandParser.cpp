#include "engine/server/ChatCommandParser.h"

#include "engine/core/Log.h"

#include <cctype>

namespace engine::server
{
	namespace
	{
		/// Trim ASCII spaces at both ends.
		std::string_view TrimAscii(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}

			while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
			{
				text.remove_suffix(1);
			}

			return text;
		}
	}

	bool ChatNameEqualsAsciiI(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
		{
			return false;
		}

		for (size_t i = 0; i < a.size(); ++i)
		{
			const unsigned char ca = static_cast<unsigned char>(a[i]);
			const unsigned char cb = static_cast<unsigned char>(b[i]);
			if (std::tolower(ca) != std::tolower(cb))
			{
				return false;
			}
		}

		return true;
	}

	const char* ChatSlashCommandLabel(ChatSlashCommandKind kind)
	{
		switch (kind)
		{
		case ChatSlashCommandKind::Who:
			return "/who";
		case ChatSlashCommandKind::Ignore:
			return "/ignore";
		case ChatSlashCommandKind::Unignore:
			return "/unignore";
		case ChatSlashCommandKind::Report:
			return "/report";
		case ChatSlashCommandKind::Kick:
			return "/kick";
		case ChatSlashCommandKind::Ban:
			return "/ban";
		case ChatSlashCommandKind::Mute:
			return "/mute";
		case ChatSlashCommandKind::Announce:
			return "/announce";
		case ChatSlashCommandKind::Friend:
			return "/friend";
		case ChatSlashCommandKind::None:
			break;
		}

		return "/?";
	}

	FriendSubCommand ParseFriendSubCommand(std::string_view argsRemainder, std::string& outTargetName)
	{
		outTargetName.clear();
		const std::string_view trimmed = TrimAscii(argsRemainder);
		if (trimmed.empty())
			return FriendSubCommand::Unknown;

		const size_t spacePos = trimmed.find(' ');
		const std::string_view sub = (spacePos == std::string_view::npos) ? trimmed : trimmed.substr(0, spacePos);

		FriendSubCommand kind = FriendSubCommand::Unknown;
		if (sub == "add")         kind = FriendSubCommand::Add;
		else if (sub == "accept") kind = FriendSubCommand::Accept;
		else if (sub == "decline")kind = FriendSubCommand::Decline;
		else if (sub == "remove") kind = FriendSubCommand::Remove;
		else
			return FriendSubCommand::Unknown;

		if (spacePos != std::string_view::npos && spacePos + 1 < trimmed.size())
		{
			const std::string_view nameView = TrimAscii(trimmed.substr(spacePos + 1));
			outTargetName.assign(nameView.begin(), nameView.end());
		}

		return kind;
	}

	bool TryParseChatSlashCommand(std::string_view text, ParsedChatSlashCommand& outCommand)
	{
		outCommand = {};
		const std::string_view line = TrimAscii(text);
		if (line.empty() || line.front() != '/')
		{
			return false;
		}

		size_t spacePos = line.find(' ');
		const std::string_view cmdToken = TrimAscii(spacePos == std::string_view::npos ? line : line.substr(0, spacePos));
		std::string_view remainder;
		if (spacePos != std::string_view::npos && spacePos + 1 < line.size())
		{
			remainder = TrimAscii(line.substr(spacePos + 1));
		}

		auto set = [&](ChatSlashCommandKind k, std::string_view rest)
		{
			outCommand.kind = k;
			outCommand.argsRemainder.assign(rest.begin(), rest.end());
		};

		if (cmdToken == "/who")
		{
			set(ChatSlashCommandKind::Who, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {} (args_len={})", ChatSlashCommandLabel(ChatSlashCommandKind::Who), remainder.size());
			return true;
		}

		if (cmdToken == "/ignore")
		{
			set(ChatSlashCommandKind::Ignore, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Ignore));
			return true;
		}

		if (cmdToken == "/unignore")
		{
			set(ChatSlashCommandKind::Unignore, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Unignore));
			return true;
		}

		if (cmdToken == "/report")
		{
			set(ChatSlashCommandKind::Report, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Report));
			return true;
		}

		if (cmdToken == "/kick")
		{
			set(ChatSlashCommandKind::Kick, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Kick));
			return true;
		}

		if (cmdToken == "/ban")
		{
			set(ChatSlashCommandKind::Ban, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Ban));
			return true;
		}

		if (cmdToken == "/mute")
		{
			set(ChatSlashCommandKind::Mute, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Mute));
			return true;
		}

		if (cmdToken == "/announce")
		{
			set(ChatSlashCommandKind::Announce, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {}", ChatSlashCommandLabel(ChatSlashCommandKind::Announce));
			return true;
		}

		if (cmdToken == "/friend")
		{
			set(ChatSlashCommandKind::Friend, remainder);
			LOG_DEBUG(Net, "[ChatCommandParser] Parsed {} (args_len={})", ChatSlashCommandLabel(ChatSlashCommandKind::Friend), remainder.size());
			return true;
		}

		return false;
	}
}
