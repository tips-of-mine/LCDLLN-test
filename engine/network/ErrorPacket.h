#pragma once

#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Parsed ERROR packet payload (error_code + optional message). request_id is in the packet header.
	struct ErrorPayload
	{
		NetErrorCode errorCode = NetErrorCode::OK;
		std::string message;
	};

	/// Builds a full protocol v1 ERROR packet (header + payload). Used by server to send errors.
	/// \param errorCode Protocol error code.
	/// \param message Optional human-readable message (avoid internal details in production).
	/// \param requestId Header request_id (e.g. match client request; 0 if no matching request).
	/// \param sessionId Header session_id (0 if not authenticated).
	/// \return Packet bytes ready to send, or empty if message exceeds max string length.
	std::vector<uint8_t> BuildErrorPacket(NetErrorCode errorCode, std::string_view message,
		uint32_t requestId, uint64_t sessionId);

	/// Parses ERROR packet payload (uint32 error_code, uint16 message_len, message_utf8).
	/// \return Parsed payload, or std::nullopt if payload is truncated or invalid.
	std::optional<ErrorPayload> ParseErrorPayload(const uint8_t* payload, size_t payloadSize);
}
