#include "engine/net/ChatSystem.h"

#include "engine/core/Log.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace engine::net
{
	namespace
	{
		/// Trim ASCII whitespace at both ends of a view.
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

		/// Returns true when \p text is non-empty and all ASCII digits.
		bool IsAllDecimalDigits(std::string_view text)
		{
			if (text.empty())
			{
				return false;
			}

			for (const char ch : text)
			{
				if (ch < '0' || ch > '9')
				{
					return false;
				}
			}

			return true;
		}

		/// Apply `/cmd ` or `/cmd` style prefix; returns false if prefix matches but has no delimiter.
		bool TryApplySimplePrefix(std::string_view line, std::string_view command, ChatChannel channel, ParsedChatCommand& out)
		{
			if (!line.starts_with(command))
			{
				return false;
			}

			if (line.size() == command.size())
			{
				out.channel = channel;
				out.messageBody.clear();
				return true;
			}

			if (line[command.size()] != ' ')
			{
				return false;
			}

			std::string_view rest = line.substr(command.size() + 1);
			rest = TrimAscii(rest);
			out.channel = channel;
			out.messageBody = std::string(rest);
			return true;
		}
	}

	bool TryDecodeChannelWire(uint8_t wire, ChatChannel& outChannel)
	{
		switch (wire)
		{
		case static_cast<uint8_t>(ChatChannel::Say):
			outChannel = ChatChannel::Say;
			return true;
		case static_cast<uint8_t>(ChatChannel::Yell):
			outChannel = ChatChannel::Yell;
			return true;
		case static_cast<uint8_t>(ChatChannel::Whisper):
			outChannel = ChatChannel::Whisper;
			return true;
		case static_cast<uint8_t>(ChatChannel::Party):
			outChannel = ChatChannel::Party;
			return true;
		case static_cast<uint8_t>(ChatChannel::Guild):
			outChannel = ChatChannel::Guild;
			return true;
		case static_cast<uint8_t>(ChatChannel::Zone):
			outChannel = ChatChannel::Zone;
			return true;
		case static_cast<uint8_t>(ChatChannel::Global):
			outChannel = ChatChannel::Global;
			return true;
		default:
			return false;
		}
	}

	uint8_t ToWire(ChatChannel channel)
	{
		return static_cast<uint8_t>(channel);
	}

	uint32_t ChannelColorArgb(ChatChannel channel)
	{
		switch (channel)
		{
		case ChatChannel::Say:
			return 0xFFFFFFFFu;
		case ChatChannel::Yell:
			return 0xFFFF0000u;
		case ChatChannel::Whisper:
			return 0xFFFFC0CBu;
		case ChatChannel::Party:
			return 0xFF2080FFu;
		case ChatChannel::Guild:
			return 0xFF00C000u;
		case ChatChannel::Zone:
			return 0xFF00CED1u;
		case ChatChannel::Global:
			return 0xFFFFD700u;
		}

		return 0xFFFFFFFFu;
	}

	std::string FormatTimeHHMMUtc(uint64_t unixMs)
	{
		const time_t seconds = static_cast<time_t>(unixMs / 1000ull);
		std::tm calendar{};
#if defined(_WIN32)
		if (gmtime_s(&calendar, &seconds) != 0)
		{
			LOG_WARN(Core, "[ChatSystem] FormatTimeHHMMUtc FAILED: gmtime_s error");
			return "??:??";
		}
#else
		if (gmtime_r(&seconds, &calendar) == nullptr)
		{
			LOG_WARN(Core, "[ChatSystem] FormatTimeHHMMUtc FAILED: gmtime_r error");
			return "??:??";
		}
#endif

		char buffer[8]{};
		if (std::snprintf(buffer, sizeof(buffer), "%02d:%02d", calendar.tm_hour, calendar.tm_min) <= 0)
		{
			LOG_WARN(Core, "[ChatSystem] FormatTimeHHMMUtc FAILED: snprintf");
			return "??:??";
		}

		return std::string(buffer);
	}

	float DistanceSquaredXZ(float ax, float az, float bx, float bz)
	{
		const float dx = ax - bx;
		const float dz = az - bz;
		return dx * dx + dz * dz;
	}

	void ChatHistoryRing::Push(const ChatMessage& message)
	{
		if (m_lines.size() >= kMaxLines)
		{
			m_lines.pop_front();
		}

		m_lines.push_back(message);
	}

	void ChatHistoryRing::Clear()
	{
		if (m_lines.empty())
		{
			return;
		}

		m_lines.clear();
		LOG_INFO(Core, "[ChatHistoryRing] Cleared");
	}

	bool ChatRateLimiter::Allow(uint32_t playerKey, std::chrono::steady_clock::time_point now)
	{
		using namespace std::chrono;
		std::deque<std::chrono::steady_clock::time_point>& stamps = m_byPlayer[playerKey].stamps;
		const auto cutoff = now - std::chrono::seconds(1);
		while (!stamps.empty() && stamps.front() < cutoff)
		{
			stamps.pop_front();
		}

		if (stamps.size() >= kMaxMessagesPerSecond)
		{
			LOG_WARN(Net, "[ChatRateLimiter] Rate limit hit (player_key={}, pending_in_window={})",
				playerKey,
				stamps.size());
			return false;
		}

		stamps.push_back(now);
		return true;
	}

	void ChatRateLimiter::Reset()
	{
		const size_t bucketCount = m_byPlayer.size();
		m_byPlayer.clear();
		LOG_INFO(Net, "[ChatRateLimiter] Reset (buckets_cleared={})", bucketCount);
	}

	bool ParseSlashPrefixes(std::string_view rawInput, ParsedChatCommand& outParsed)
	{
		outParsed = {};
		std::string_view line = TrimAscii(rawInput);
		if (line.empty())
		{
			return false;
		}

		if (!line.starts_with('/'))
		{
			outParsed.channel = ChatChannel::Say;
			outParsed.messageBody = std::string(line);
			return true;
		}

		if (TryApplySimplePrefix(line, "/s", ChatChannel::Say, outParsed))
		{
			return true;
		}

		if (TryApplySimplePrefix(line, "/y", ChatChannel::Yell, outParsed))
		{
			return true;
		}

		if (TryApplySimplePrefix(line, "/p", ChatChannel::Party, outParsed))
		{
			return true;
		}

		if (TryApplySimplePrefix(line, "/g", ChatChannel::Guild, outParsed))
		{
			return true;
		}

		if (TryApplySimplePrefix(line, "/z", ChatChannel::Zone, outParsed))
		{
			return true;
		}

		if (line.starts_with("/gl"))
		{
			if (line.size() == 3)
			{
				outParsed.channel = ChatChannel::Global;
				outParsed.messageBody.clear();
				return true;
			}

			if (line.size() > 3 && line[3] != ' ')
			{
				outParsed.channel = ChatChannel::Say;
				outParsed.messageBody = std::string(line);
				return true;
			}

			std::string_view rest = line.substr(4);
			rest = TrimAscii(rest);
			outParsed.channel = ChatChannel::Global;
			outParsed.messageBody = std::string(rest);
			return true;
		}

		if (line.starts_with("/w"))
		{
			if (line.size() > 2 && line[2] != ' ')
			{
				outParsed.channel = ChatChannel::Say;
				outParsed.messageBody = std::string(line);
				return true;
			}

			std::string_view rest = line.size() > 2 ? line.substr(3) : std::string_view{};
			rest = TrimAscii(rest);
			outParsed.channel = ChatChannel::Whisper;
			const size_t spacePos = rest.find(' ');
			if (spacePos == std::string_view::npos)
			{
				outParsed.whisperTargetToken = std::string(rest);
				outParsed.messageBody.clear();
			}
			else
			{
				outParsed.whisperTargetToken = std::string(TrimAscii(rest.substr(0, spacePos)));
				outParsed.messageBody = std::string(TrimAscii(rest.substr(spacePos + 1)));
			}

			return true;
		}

		outParsed.channel = ChatChannel::Say;
		outParsed.messageBody = std::string(line);
		return true;
	}

	uint64_t TryParseWhisperEntityId(std::string_view token)
	{
		if (!IsAllDecimalDigits(token))
		{
			return 0;
		}

		uint64_t value = 0;
		const char* const first = token.data();
		const char* const last = first + token.size();
		const auto result = std::from_chars(first, last, value);
		if (result.ec != std::errc{} || result.ptr != last)
		{
			LOG_WARN(Core, "[ChatSystem] TryParseWhisperEntityId: parse FAILED (token_len={})", token.size());
			return 0;
		}

		return value;
	}
}
