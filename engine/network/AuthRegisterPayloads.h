#pragma once

#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// Parsed AUTH_REQUEST payload (protocol_v1: username_len, username_utf8, password_len, password_utf8).
	/// The password field is interpreted as client_hash (Argon2 encoded string) per M20.2.
	struct AuthRequestPayload
	{
		std::string login;
		std::string client_hash;
	};

	/// Parsed REGISTER_REQUEST payload: login, optional email, client_hash (password field).
	struct RegisterRequestPayload
	{
		std::string login;
		std::string email;
		std::string client_hash;
	};

	/// Parses AUTH_REQUEST payload. Returns nullopt if truncated or invalid.
	std::optional<AuthRequestPayload> ParseAuthRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parses REGISTER_REQUEST payload (login, email, client_hash). Returns nullopt if truncated or invalid.
	std::optional<RegisterRequestPayload> ParseRegisterRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds AUTH_REQUEST payload (for client). Protocol: username_len, username_utf8, password_len, password_utf8 (client_hash).
	std::vector<uint8_t> BuildAuthRequestPayload(std::string_view login, std::string_view client_hash);

	/// Builds REGISTER_REQUEST payload (for client): login, email, client_hash (each as length-prefixed string).
	std::vector<uint8_t> BuildRegisterRequestPayload(std::string_view login, std::string_view email, std::string_view client_hash);

	/// Parsed AUTH_RESPONSE payload: success, session_id (if success), server_time_sec, version_gate, error_code (if fail).
	struct AuthResponsePayload
	{
		uint8_t success = 0;
		uint64_t session_id = 0;
		uint64_t server_time_sec = 0;
		uint32_t version_gate = 0;
		NetErrorCode error_code = NetErrorCode::OK;
	};
	/// Parses AUTH_RESPONSE payload. Returns nullopt if truncated.
	std::optional<AuthResponsePayload> ParseAuthResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Parsed REGISTER_RESPONSE payload.
	struct RegisterResponsePayload
	{
		uint8_t success = 0;
		NetErrorCode error_code = NetErrorCode::OK;
	};
	std::optional<RegisterResponsePayload> ParseRegisterResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Builds AUTH_RESPONSE packet (success path). success=1, session_id, server_time_sec, version_gate in payload.
	/// responseHeaderSessionId: session_id in response packet header (0 before auth).
	std::vector<uint8_t> BuildAuthResponsePacket(uint8_t success, uint64_t sessionId, uint64_t serverTimeSec,
		uint32_t versionGate, uint32_t requestId, uint64_t responseHeaderSessionId);
	/// Builds AUTH_RESPONSE packet (failure path) or ERROR packet; here we send ERROR packet for consistency.
	std::vector<uint8_t> BuildAuthResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t responseHeaderSessionId);

	/// Builds REGISTER_RESPONSE packet. success: 1=ok, 0=fail. If fail, error_code in payload.
	std::vector<uint8_t> BuildRegisterResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildRegisterResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);
}
