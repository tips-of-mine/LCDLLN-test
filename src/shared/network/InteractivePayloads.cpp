// M100.32 — Implémentation Parse/Build des payloads Interactive Props.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> ne contenant que le
//     payload (sans header protocol_v1). Utilisé pour tests round-trip et
//     pour les requests côté client.
//   - Build*Packet utilise PacketBuilder pour assembler le paquet complet
//     header + payload, prêt à passer à NetServer::Send.
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/InteractivePayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	// -------------------------------------------------------------------------
	// INTERACTIVE_STATE_CHANGE (200)
	// -------------------------------------------------------------------------

	std::optional<InteractiveStateChangePayload> ParseInteractiveStateChangePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint64 id (8) + uint8 newState (1) + uint64 clientTimeMs (8) = 17.
		if (!payload || payloadSize < 17u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		InteractiveStateChangePayload out;
		uint8_t stateByte = 0;
		if (!r.ReadU64(out.id))            return std::nullopt;
		if (!r.ReadBytes(&stateByte, 1u))  return std::nullopt;
		out.newState = stateByte;
		if (!r.ReadU64(out.clientTimeMs))  return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildInteractiveStateChangePayload(uint64_t id, uint8_t newState, uint64_t clientTimeMs)
	{
		std::vector<uint8_t> buf(17u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(id))                 return {};
		if (!w.WriteBytes(&newState, 1u))    return {};
		if (!w.WriteU64(clientTimeMs))       return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildInteractiveStateChangePacket(uint64_t id, uint8_t newState, uint64_t clientTimeMs,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(id))               return {};
		if (!w.WriteBytes(&newState, 1u))  return {};
		if (!w.WriteU64(clientTimeMs))     return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpInteractiveStateChange, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// INTERACTIVE_STATE_BROADCAST (201)
	// -------------------------------------------------------------------------

	std::optional<InteractiveStateBroadcastPayload> ParseInteractiveStateBroadcastPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 17u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		InteractiveStateBroadcastPayload out;
		uint8_t stateByte = 0;
		if (!r.ReadU64(out.id))            return std::nullopt;
		if (!r.ReadBytes(&stateByte, 1u))  return std::nullopt;
		out.newState = stateByte;
		if (!r.ReadU64(out.serverTimeMs))  return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildInteractiveStateBroadcastPayload(uint64_t id, uint8_t newState, uint64_t serverTimeMs)
	{
		std::vector<uint8_t> buf(17u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(id))               return {};
		if (!w.WriteBytes(&newState, 1u))  return {};
		if (!w.WriteU64(serverTimeMs))     return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildInteractiveStateBroadcastPacket(uint64_t id, uint8_t newState, uint64_t serverTimeMs,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(id))               return {};
		if (!w.WriteBytes(&newState, 1u))  return {};
		if (!w.WriteU64(serverTimeMs))     return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpInteractiveStateBroadcast, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// INTERACTIVE_STATE_SYNC (202)
	// -------------------------------------------------------------------------

	std::optional<InteractiveStateSyncPayload> ParseInteractiveStateSyncPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 2u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		InteractiveStateSyncPayload out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.entries.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			InteractiveSyncEntry e;
			uint8_t stateByte = 0;
			if (!r.ReadU64(e.id))             return std::nullopt;
			if (!r.ReadBytes(&stateByte, 1u)) return std::nullopt;
			e.state = stateByte;
			out.entries.push_back(e);
		}
		return out;
	}

	namespace
	{
		bool WriteSyncBody(ByteWriter& w, const std::vector<InteractiveSyncEntry>& entries)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(entries.size()))) return false;
			for (const auto& e : entries)
			{
				if (!w.WriteU64(e.id))            return false;
				if (!w.WriteBytes(&e.state, 1u))  return false;
			}
			return true;
		}
	}

	std::vector<uint8_t> BuildInteractiveStateSyncPayload(const std::vector<InteractiveSyncEntry>& entries)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSyncBody(w, entries)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildInteractiveStateSyncPacket(const std::vector<InteractiveSyncEntry>& entries,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSyncBody(w, entries)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpInteractiveStateSync, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
