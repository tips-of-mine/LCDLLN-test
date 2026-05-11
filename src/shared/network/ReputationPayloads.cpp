// CMANGOS.24 (Phase 3.24 step 3+4) — Implementation Parse/Build des payloads Reputation.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket utilise PacketBuilder pour assembler le paquet
//     complet header + payload, pret a passer a NetServer::Send.
//   - Parse* lit le payload nu (sans header).
//
// Note : ByteWriter/ByteReader n'ont pas WriteI32/ReadI32. On serialise les
// int32 via static_cast<uint32_t>(value) et on lit via static_cast<int32_t>.
// Le pattern bit-pour-bit est conserve (two's complement).

#include "src/shared/network/ReputationPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit une entree (uint32 factionId, int32 value, int8 standing).
		bool WriteEntry(ByteWriter& w, const ReputationEntry& e)
		{
			if (!w.WriteU32(e.factionId))                          return false;
			if (!w.WriteU32(static_cast<uint32_t>(e.value)))       return false;
			const uint8_t standingByte = static_cast<uint8_t>(e.standing);
			if (!w.WriteBytes(&standingByte, 1u))                  return false;
			return true;
		}

		/// Lit une entree (uint32 factionId, int32 value, int8 standing).
		bool ReadEntry(ByteReader& r, ReputationEntry& e)
		{
			if (!r.ReadU32(e.factionId))                           return false;
			uint32_t valueRaw = 0;
			if (!r.ReadU32(valueRaw))                              return false;
			e.value = static_cast<int32_t>(valueRaw);
			uint8_t standingByte = 0;
			if (!r.ReadBytes(&standingByte, 1u))                   return false;
			e.standing = static_cast<int8_t>(standingByte);
			return true;
		}

		/// Serialise la partie « apres error » de REPUTATION_LIST_RESPONSE.
		bool WriteListBody(ByteWriter& w, uint8_t error, const std::vector<ReputationEntry>& entries)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(entries.size())))
				return false;
			for (const auto& e : entries)
			{
				if (!WriteEntry(w, e))
					return false;
			}
			return true;
		}

		/// Serialise un push UPDATE_NOTIFICATION.
		bool WriteUpdateBody(ByteWriter& w, uint32_t factionId, int32_t newValue, int8_t newStanding, int32_t delta)
		{
			if (!w.WriteU32(factionId))                                   return false;
			if (!w.WriteU32(static_cast<uint32_t>(newValue)))             return false;
			const uint8_t standingByte = static_cast<uint8_t>(newStanding);
			if (!w.WriteBytes(&standingByte, 1u))                         return false;
			if (!w.WriteU32(static_cast<uint32_t>(delta)))                return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// REPUTATION_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<ReputationListRequestPayload> ParseReputationListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return ReputationListRequestPayload{};
	}

	std::vector<uint8_t> BuildReputationListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// REPUTATION_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<ReputationListResponsePayload> ParseReputationListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ReputationListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.entries.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			ReputationEntry e;
			if (!ReadEntry(r, e))
				return std::nullopt;
			out.entries.push_back(e);
		}
		return out;
	}

	std::vector<uint8_t> BuildReputationListResponsePayload(uint8_t error, const std::vector<ReputationEntry>& entries)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteListBody(w, error, entries))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildReputationListResponsePacket(uint8_t error, const std::vector<ReputationEntry>& entries,
	                                                       uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteListBody(w, error, entries))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeReputationListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// REPUTATION_UPDATE_NOTIFICATION (push, request_id=0)
	// -------------------------------------------------------------------------

	std::optional<ReputationUpdateNotificationPayload> ParseReputationUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint32 factionId (4) + int32 newValue (4) + int8 newStanding (1) + int32 delta (4) = 13.
		if (!payload || payloadSize < 13u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		ReputationUpdateNotificationPayload out;
		if (!r.ReadU32(out.factionId))            return std::nullopt;
		uint32_t valueRaw = 0;
		if (!r.ReadU32(valueRaw))                 return std::nullopt;
		out.newValue = static_cast<int32_t>(valueRaw);
		uint8_t standingByte = 0;
		if (!r.ReadBytes(&standingByte, 1u))      return std::nullopt;
		out.newStanding = static_cast<int8_t>(standingByte);
		uint32_t deltaRaw = 0;
		if (!r.ReadU32(deltaRaw))                 return std::nullopt;
		out.delta = static_cast<int32_t>(deltaRaw);
		return out;
	}

	std::vector<uint8_t> BuildReputationUpdateNotificationPayload(uint32_t factionId, int32_t newValue, int8_t newStanding, int32_t delta)
	{
		std::vector<uint8_t> buf(13u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteUpdateBody(w, factionId, newValue, newStanding, delta))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildReputationUpdateNotificationPacket(uint32_t factionId, int32_t newValue, int8_t newStanding, int32_t delta,
	                                                              uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteUpdateBody(w, factionId, newValue, newStanding, delta))
			return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeReputationUpdateNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
