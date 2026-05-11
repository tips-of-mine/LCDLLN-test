// CMANGOS.25 (Phase 3.25 step 3+4) — Implémentation Parse/Build des payloads
// IgnoreList. Convention symétrique aux autres *Payloads.cpp du repo.

#include "src/shared/network/IgnoreListPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Helper : scratch buffer dimensionné à la limite protocole pour les
		/// payloads contenant un vector (List). Tronqué à l'offset réel via resize.
		std::vector<uint8_t> MakeScratchBuffer()
		{
			return std::vector<uint8_t>(kProtocolV1MaxPacketSize, 0u);
		}
	}

	// -------------------------------------------------------------------------
	// IGNORE_ADD
	// -------------------------------------------------------------------------

	std::optional<IgnoreAddRequestPayload> ParseIgnoreAddRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		IgnoreAddRequestPayload out;
		if (!r.ReadU64(out.targetAccountId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildIgnoreAddRequestPayload(uint64_t targetAccountId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(targetAccountId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<IgnoreAddResponsePayload> ParseIgnoreAddResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 targetAccountId (8) = 9 octets.
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		IgnoreAddResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))         return std::nullopt;
		if (!r.ReadU64(out.targetAccountId))      return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildIgnoreAddResponsePayload(uint8_t error, uint64_t targetAccountId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))           return {};
		if (!w.WriteU64(targetAccountId))        return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildIgnoreAddResponsePacket(uint8_t error, uint64_t targetAccountId,
	                                                  uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))           return {};
		if (!w.WriteU64(targetAccountId))        return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeIgnoreAddResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// IGNORE_REMOVE
	// -------------------------------------------------------------------------

	std::optional<IgnoreRemoveRequestPayload> ParseIgnoreRemoveRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		IgnoreRemoveRequestPayload out;
		if (!r.ReadU64(out.targetAccountId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildIgnoreRemoveRequestPayload(uint64_t targetAccountId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(targetAccountId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<IgnoreRemoveResponsePayload> ParseIgnoreRemoveResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		IgnoreRemoveResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))        return std::nullopt;
		if (!r.ReadU64(out.targetAccountId))     return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildIgnoreRemoveResponsePayload(uint8_t error, uint64_t targetAccountId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))           return {};
		if (!w.WriteU64(targetAccountId))        return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildIgnoreRemoveResponsePacket(uint8_t error, uint64_t targetAccountId,
	                                                     uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))           return {};
		if (!w.WriteU64(targetAccountId))        return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeIgnoreRemoveResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// IGNORE_LIST
	// -------------------------------------------------------------------------

	std::optional<IgnoreListRequestPayload> ParseIgnoreListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepté (0 octet ou plus, ignore le reste).
		return IgnoreListRequestPayload{};
	}

	std::vector<uint8_t> BuildIgnoreListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	std::optional<IgnoreListResponsePayload> ParseIgnoreListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1). Si error=0, on continue avec uint16 count + uint64 * count.
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		IgnoreListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.ignoredAccountIds.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			uint64_t accId = 0;
			if (!r.ReadU64(accId))
				return std::nullopt;
			out.ignoredAccountIds.push_back(accId);
		}
		return out;
	}

	namespace
	{
		/// Sérialise la partie « après error » de IGNORE_LIST_RESPONSE.
		/// Mutualisé entre payload-only et packet builder.
		bool WriteListBody(ByteWriter& w, uint8_t error, const std::vector<uint64_t>& ignored)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(ignored.size())))
				return false;
			for (const auto accId : ignored)
			{
				if (!w.WriteU64(accId))
					return false;
			}
			return true;
		}
	}

	std::vector<uint8_t> BuildIgnoreListResponsePayload(uint8_t error, const std::vector<uint64_t>& ignoredAccountIds)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!WriteListBody(w, error, ignoredAccountIds))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildIgnoreListResponsePacket(uint8_t error,
	                                                    const std::vector<uint64_t>& ignoredAccountIds,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteListBody(w, error, ignoredAccountIds))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeIgnoreListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
