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
	/// M33.3: optional captcha_token appended as a 4th length-prefixed string (backward-compatible).
	/// Optional 5th string: locale tag (e.g. "en", "fr") for transactional emails; empty = English.
	struct RegisterRequestPayload
	{
		std::string login;
		std::string email;
		std::string client_hash;
		std::string first_name;
		std::string last_name;
		std::string birth_date;    ///< yyyy-mm-dd
		std::string captcha_token; ///< M33.3: CAPTCHA response token from client widget. Empty when absent.
		std::string locale_tag;    ///< ISO-style language code for account email locale.
		std::string country_code; ///< Code ISO-2 pays saisi à l'inscription (ex. "FR"). Peut être vide.
	};

	/// Parses AUTH_REQUEST payload. Returns nullopt if truncated or invalid.
	std::optional<AuthRequestPayload> ParseAuthRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parses REGISTER_REQUEST payload (login, email, client_hash). Returns nullopt if truncated or invalid.
	std::optional<RegisterRequestPayload> ParseRegisterRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds AUTH_REQUEST payload (for client). Protocol: username_len, username_utf8, password_len, password_utf8 (client_hash).
	std::vector<uint8_t> BuildAuthRequestPayload(std::string_view login, std::string_view client_hash);

	/// Builds REGISTER_REQUEST payload: login, client_hash, email; optional captcha and locale (see implementation).
	std::vector<uint8_t> BuildRegisterRequestPayload(std::string_view login, std::string_view email, std::string_view client_hash,
	                                                 std::string_view first_name = {},
	                                                 std::string_view last_name = {},
	                                                 std::string_view birth_date = {},
	                                                 std::string_view captcha_token = {},
	                                                 std::string_view locale_tag = {},
	                                                 std::string_view country_code = {});

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
		uint64_t account_id = 0;
		NetErrorCode error_code = NetErrorCode::OK;
		std::string tag_id; ///< TAG-ID généré (ex. "FR60400123"). Vide en cas d'erreur.
	};
	std::optional<RegisterResponsePayload> ParseRegisterResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Builds AUTH_RESPONSE packet (success path). success=1, session_id, server_time_sec, version_gate in payload.
	/// responseHeaderSessionId: session_id in response packet header (0 before auth).
	std::vector<uint8_t> BuildAuthResponsePacket(uint8_t success, uint64_t sessionId, uint64_t serverTimeSec,
		uint32_t versionGate, uint32_t requestId, uint64_t responseHeaderSessionId);
	/// Builds AUTH_RESPONSE packet (failure path) or ERROR packet; here we send ERROR packet for consistency.
	std::vector<uint8_t> BuildAuthResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t responseHeaderSessionId);

	/// Builds REGISTER_RESPONSE packet. success: 1=ok, 0=fail. If fail, error_code in payload.
	std::vector<uint8_t> BuildRegisterResponsePacket(uint8_t success, uint64_t accountId,
	    std::string_view tag_id, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildRegisterResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// M33.2 — Password reset + email verification payloads
	// -------------------------------------------------------------------------

	/// Client→Master FORGOT_PASSWORD_REQUEST payload: email to send reset link to.
	struct ForgotPasswordRequestPayload
	{
		std::string email;
	};
	/// Parses FORGOT_PASSWORD_REQUEST payload. Returns nullopt if truncated/invalid.
	std::optional<ForgotPasswordRequestPayload> ParseForgotPasswordRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildForgotPasswordRequestPayload(std::string_view email);

	/// Builds FORGOT_PASSWORD_RESPONSE packet. Always success=1 (avoid email enumeration).
	std::vector<uint8_t> BuildForgotPasswordResponsePacket(uint32_t requestId, uint64_t sessionIdHeader);

	/// Client→Master RESET_PASSWORD_REQUEST payload: reset token + new client_hash.
	struct ResetPasswordRequestPayload
	{
		std::string token;        ///< 32-char hex reset token from email link.
		std::string new_client_hash; ///< Argon2-encoded new password hash from client.
	};
	/// Parses RESET_PASSWORD_REQUEST payload. Returns nullopt if truncated/invalid.
	std::optional<ResetPasswordRequestPayload> ParseResetPasswordRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildResetPasswordRequestPayload(std::string_view token, std::string_view new_client_hash);

	/// Builds RESET_PASSWORD_RESPONSE packet. success=1 OK, 0=error.
	std::vector<uint8_t> BuildResetPasswordResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildResetPasswordResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	/// Client→Master VERIFY_EMAIL_REQUEST payload: account_id + 6-digit code.
	struct VerifyEmailRequestPayload
	{
		uint64_t account_id = 0;
		std::string code; ///< 6-digit numeric code as string.
	};
	/// Parses VERIFY_EMAIL_REQUEST payload. Returns nullopt if truncated/invalid.
	std::optional<VerifyEmailRequestPayload> ParseVerifyEmailRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildVerifyEmailRequestPayload(uint64_t account_id, std::string_view code);

	/// Builds VERIFY_EMAIL_RESPONSE packet. success=1 OK, 0=error.
	std::vector<uint8_t> BuildVerifyEmailResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildVerifyEmailResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);
}
