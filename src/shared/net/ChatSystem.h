#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::net
{
	/// Authoritative chat channel list (wire values must stay stable).
	enum class ChatChannel : uint8_t
	{
		Say = 0,
		Yell = 1,
		Whisper = 2,
		Party = 3,
		Guild = 4,
		Zone = 5,
		Global = 6,
		/// Messages broadcast by the hosting game server (e.g. events, restarts, zone notices).
		/// Clients cannot write to this channel; senderEntityId is always 0.
		Server = 7,
		/// Multi-group raid channel (broadcast to all parties in the same raid).
		Raid = 8,
		/// Friends-list private channel (broadcast only to online friends of the sender).
		Friends = 9
	};

	/// One chat line stored client- or server-side (history / relay).
	struct ChatMessage
	{
		uint64_t timestampUnixMs = 0;
		ChatChannel channel = ChatChannel::Say;
		std::string sender;
		std::string text;
	};

	/// Decode one wire channel byte; returns false when unknown.
	bool TryDecodeChannelWire(uint8_t wire, ChatChannel& outChannel);

	/// Encode channel for wire payloads.
	uint8_t ToWire(ChatChannel channel);

	/// ARGB8888 color associated to a channel (UI / debug panel).
	uint32_t ChannelColorArgb(ChatChannel channel);

	/// Format Unix epoch milliseconds as UTC HH:MM (chat timestamp).
	std::string FormatTimeHHMMUtc(uint64_t unixMs);

	/// Squared horizontal distance (XZ) between two world positions (meters).
	float DistanceSquaredXZ(float ax, float az, float bx, float bz);

	inline constexpr float kChatSayRadiusMeters = 25.0f;
	inline constexpr float kChatYellRadiusMeters = 200.0f;

	/// Ring buffer of the last \ref kMaxLines chat messages (M29.1 DoD: 500).
	class ChatHistoryRing final
	{
	public:
		static constexpr size_t kMaxLines = 500;

		ChatHistoryRing() = default;

		/// Append one message; drops oldest when over capacity.
		void Push(const ChatMessage& message);

		/// Read-only view of stored lines in chronological order (oldest first).
		const std::deque<ChatMessage>& Lines() const { return m_lines; }

		/// Remove all lines (e.g. on full UI reset).
		void Clear();

	private:
		std::deque<ChatMessage> m_lines;
	};

	/// Token-bucket style limiter: max \ref kMaxMessagesPerSecond per player key.
	class ChatRateLimiter final
	{
	public:
		static constexpr uint32_t kMaxMessagesPerSecond = 5;

		ChatRateLimiter() = default;

		/// Returns true if the send is allowed, false if rate-limited.
		bool Allow(uint32_t playerKey, std::chrono::steady_clock::time_point now);

		/// Clear all windows (e.g. server shutdown).
		void Reset();

	private:
		struct StampBucket
		{
			std::deque<std::chrono::steady_clock::time_point> stamps;
		};

		std::unordered_map<uint32_t, StampBucket> m_byPlayer;
	};

	/// Result of parsing slash-prefixed chat from the input field.
	struct ParsedChatCommand final
	{
		ChatChannel channel = ChatChannel::Say;
		/// For whisper: first token after `/w` when treated as name token (may be numeric entity id).
		std::string whisperTargetToken;
		std::string messageBody;
	};

	/// Parse `/s`, `/y`, `/w <token>`, `/p`, `/g`, `/z` (zone), `/gl` (global). Plain text defaults to Say.
	bool ParseSlashPrefixes(std::string_view rawInput, ParsedChatCommand& outParsed);

	/// If \p token is all decimal digits, parse as uint64 entity id; otherwise 0.
	uint64_t TryParseWhisperEntityId(std::string_view token);
}
