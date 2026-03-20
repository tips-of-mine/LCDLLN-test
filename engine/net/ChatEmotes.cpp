#include "engine/net/ChatEmotes.h"

#include "engine/core/Log.h"

#include <cctype>
#include <string_view>

namespace engine::net
{
	namespace
	{
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

		bool EqualsAsciiI(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			for (size_t i = 0; i < a.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				{
					return false;
				}
			}

			return true;
		}
	}

	uint8_t ToWire(ChatEmoteWireId emote)
	{
		return static_cast<uint8_t>(emote);
	}

	bool TryFromWire(uint8_t wire, ChatEmoteWireId& outEmote)
	{
		const auto e = static_cast<ChatEmoteWireId>(wire);
		switch (e)
		{
		case ChatEmoteWireId::Dance:
		case ChatEmoteWireId::Wave:
		case ChatEmoteWireId::Laugh:
		case ChatEmoteWireId::Cry:
		case ChatEmoteWireId::Sit:
		case ChatEmoteWireId::Bow:
		case ChatEmoteWireId::Point:
		case ChatEmoteWireId::Salute:
			outEmote = e;
			return true;
		default:
			outEmote = ChatEmoteWireId::None;
			return false;
		}
	}

	const char* ChatEmoteName(ChatEmoteWireId emote)
	{
		switch (emote)
		{
		case ChatEmoteWireId::Dance:
			return "dance";
		case ChatEmoteWireId::Wave:
			return "wave";
		case ChatEmoteWireId::Laugh:
			return "laugh";
		case ChatEmoteWireId::Cry:
			return "cry";
		case ChatEmoteWireId::Sit:
			return "sit";
		case ChatEmoteWireId::Bow:
			return "bow";
		case ChatEmoteWireId::Point:
			return "point";
		case ChatEmoteWireId::Salute:
			return "salute";
		case ChatEmoteWireId::None:
			break;
		}

		return "none";
	}

	bool ChatEmoteLoops(ChatEmoteWireId emote)
	{
		return emote == ChatEmoteWireId::Sit;
	}

	float ChatEmoteOneShotDurationSeconds(ChatEmoteWireId emote)
	{
		if (ChatEmoteLoops(emote))
		{
			return 0.0f;
		}

		switch (emote)
		{
		case ChatEmoteWireId::Dance:
			return 4.0f;
		case ChatEmoteWireId::Wave:
			return 2.5f;
		case ChatEmoteWireId::Laugh:
			return 3.0f;
		case ChatEmoteWireId::Cry:
			return 3.5f;
		case ChatEmoteWireId::Bow:
			return 2.5f;
		case ChatEmoteWireId::Point:
			return 2.0f;
		case ChatEmoteWireId::Salute:
			return 2.5f;
		case ChatEmoteWireId::None:
		default:
			return 0.0f;
		}
	}

	bool TryParseEmoteChatLine(std::string_view text, ParsedEmoteChatLine& outParsed)
	{
		outParsed = {};
		const std::string_view line = TrimAscii(text);
		if (line.empty() || line.front() != '/')
		{
			return false;
		}

		const std::string_view token = line;
		auto match = [&](std::string_view lit, ChatEmoteWireId id)
		{
			if (!EqualsAsciiI(token, lit))
			{
				return false;
			}

			outParsed.emote = id;
			outParsed.loop = ChatEmoteLoops(id);
			LOG_DEBUG(Net, "[ChatEmotes] Parsed /{} (loop={})", ChatEmoteName(id), outParsed.loop ? "yes" : "no");
			return true;
		};

		if (match("/dance", ChatEmoteWireId::Dance))
		{
			return true;
		}

		if (match("/wave", ChatEmoteWireId::Wave))
		{
			return true;
		}

		if (match("/laugh", ChatEmoteWireId::Laugh))
		{
			return true;
		}

		if (match("/cry", ChatEmoteWireId::Cry))
		{
			return true;
		}

		if (match("/sit", ChatEmoteWireId::Sit))
		{
			return true;
		}

		if (match("/bow", ChatEmoteWireId::Bow))
		{
			return true;
		}

		if (match("/point", ChatEmoteWireId::Point))
		{
			return true;
		}

		if (match("/salute", ChatEmoteWireId::Salute))
		{
			return true;
		}

		return false;
	}
}
