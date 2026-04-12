#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	std::optional<AuthRequestPayload> ParseAuthRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuthRequestPayload out;
		if (!r.ReadString(out.login) || !r.ReadString(out.client_hash))
			return std::nullopt;
		return out;
	}

	std::optional<RegisterRequestPayload> ParseRegisterRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		RegisterRequestPayload out;
		if (!r.ReadString(out.login) || !r.ReadString(out.client_hash))
			return std::nullopt;
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.email))
				return std::nullopt;
		}
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.first_name))
				return std::nullopt;
		}
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.last_name))
				return std::nullopt;
		}
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.birth_date))
				return std::nullopt;
		}
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.captcha_token))
				return std::nullopt;
		}
		if (r.Remaining() >= 2u)
		{
			if (!r.ReadString(out.locale_tag))
				return std::nullopt;
		}
		// country_code : optionnel (backward-compatible ; absent dans les anciens clients).
		(void)r.ReadString(out.country_code); // échec silencieux = chaîne vide
		return out;
	}

	std::vector<uint8_t> BuildAuthRequestPayload(std::string_view login, std::string_view client_hash)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(login) || !w.WriteString(client_hash))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildRegisterRequestPayload(std::string_view login, std::string_view email, std::string_view client_hash,
	                                                 std::string_view first_name, std::string_view last_name,
	                                                 std::string_view birth_date, std::string_view captcha_token, std::string_view locale_tag,
	                                                 std::string_view country_code)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(login) || !w.WriteString(client_hash) || !w.WriteString(email))
			return {};
		if (!first_name.empty() || !last_name.empty() || !birth_date.empty() || !captcha_token.empty() || !locale_tag.empty() || !country_code.empty())
		{
			if (!w.WriteString(first_name) || !w.WriteString(last_name) || !w.WriteString(birth_date))
				return {};
			if (!w.WriteString(captcha_token))
				return {};
			if (!w.WriteString(locale_tag))
				return {};
			w.WriteString(country_code);
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<AuthResponsePayload> ParseAuthResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuthResponsePayload out;
		if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&out.success), 1))
			return std::nullopt;
		if (out.success != 0)
		{
			if (r.Remaining() < 8u + 8u + 4u || !r.ReadU64(out.session_id) || !r.ReadU64(out.server_time_sec) || !r.ReadU32(out.version_gate))
				return std::nullopt;
		}
		else if (r.Remaining() >= 4u)
		{
			uint32_t code = 0;
			if (!r.ReadU32(code))
				return std::nullopt;
			out.error_code = static_cast<NetErrorCode>(code);
		}
		return out;
	}

	std::optional<RegisterResponsePayload> ParseRegisterResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		RegisterResponsePayload out;
		if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&out.success), 1))
			return std::nullopt;
		if (out.success == 0 && r.Remaining() >= 4u)
		{
			uint32_t code = 0;
			if (!r.ReadU32(code))
				return std::nullopt;
			out.error_code = static_cast<NetErrorCode>(code);
		}
		else if (out.success != 0 && r.Remaining() >= 8u)
		{
			if (!r.ReadU64(out.account_id))
				return std::nullopt;
		}
		if (out.success != 0)
			(void)r.ReadString(out.tag_id); // optionnel, backward-compatible (absent sur anciens serveurs)
		return out;
	}

	std::vector<uint8_t> BuildAuthResponsePacket(uint8_t success, uint64_t sessionId, uint64_t serverTimeSec,
		uint32_t versionGate, uint32_t requestId, uint64_t responseHeaderSessionId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1) || !w.WriteU64(sessionId) || !w.WriteU64(serverTimeSec) || !w.WriteU32(versionGate))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuthResponse, 0, requestId, responseHeaderSessionId, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildAuthResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t responseHeaderSessionId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint8_t zero = 0;
		if (!w.WriteBytes(&zero, 1) || !w.WriteU32(static_cast<uint32_t>(errorCode)))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuthResponse, 0, requestId, responseHeaderSessionId, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildRegisterResponsePacket(uint8_t success, uint64_t accountId,
	    std::string_view tag_id, uint32_t requestId, uint64_t responseHeaderSessionId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1))
			return {};
		if (success != 0 && !w.WriteU64(accountId))
			return {};
		if (success != 0)
			w.WriteString(tag_id);
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeRegisterResponse, 0, requestId, responseHeaderSessionId, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildRegisterResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t responseHeaderSessionId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint8_t zero = 0;
		if (!w.WriteBytes(&zero, 1) || !w.WriteU32(static_cast<uint32_t>(errorCode)))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeRegisterResponse, 0, requestId, responseHeaderSessionId, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// M33.2 — Password reset + email verification payloads
	// -------------------------------------------------------------------------

	std::optional<ForgotPasswordRequestPayload> ParseForgotPasswordRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ForgotPasswordRequestPayload out;
		if (!r.ReadString(out.email))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildForgotPasswordRequestPayload(std::string_view email)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(email))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildForgotPasswordResponsePacket(uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint8_t one = 1;
		if (!w.WriteBytes(&one, 1))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeForgotPasswordResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::optional<ResetPasswordRequestPayload> ParseResetPasswordRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ResetPasswordRequestPayload out;
		if (!r.ReadString(out.token) || !r.ReadString(out.new_client_hash))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildResetPasswordRequestPayload(std::string_view token, std::string_view new_client_hash)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(token) || !w.WriteString(new_client_hash))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildResetPasswordResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeResetPasswordResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildResetPasswordResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint8_t zero = 0;
		if (!w.WriteBytes(&zero, 1) || !w.WriteU32(static_cast<uint32_t>(errorCode)))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeResetPasswordResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::optional<VerifyEmailRequestPayload> ParseVerifyEmailRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		VerifyEmailRequestPayload out;
		if (!r.ReadU64(out.account_id) || !r.ReadString(out.code))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildVerifyEmailRequestPayload(uint64_t account_id, std::string_view code)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(account_id) || !w.WriteString(code))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildVerifyEmailResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeVerifyEmailResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildVerifyEmailResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		const uint8_t zero = 0;
		if (!w.WriteBytes(&zero, 1) || !w.WriteU32(static_cast<uint32_t>(errorCode)))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeVerifyEmailResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
