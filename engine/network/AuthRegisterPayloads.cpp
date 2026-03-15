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

	std::vector<uint8_t> BuildRegisterRequestPayload(std::string_view login, std::string_view email, std::string_view client_hash)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(login) || !w.WriteString(client_hash) || !w.WriteString(email))
			return {};
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

	std::vector<uint8_t> BuildRegisterResponsePacket(uint8_t success, uint32_t requestId, uint64_t responseHeaderSessionId)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&success, 1))
			return {};
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
}
