// CMANGOS.30 (Phase 5.30 step 3+4) — Implementation Parse/Build des payloads
// Cinematics (108-112).
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client.
//   - Build*Packet utilise PacketBuilder pour assembler le paquet complet
//     header + payload, pret a passer a NetServer::Send.
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/CinematicPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	// -------------------------------------------------------------------------
	// CINEMATIC_PLAY_NOTIFICATION (push, 108)
	// -------------------------------------------------------------------------

	std::optional<CinematicPlayNotificationPayload> ParseCinematicPlayNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 sequenceId (4) + uint8 reason (1) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		CinematicPlayNotificationPayload out;
		if (!r.ReadU32(out.sequenceId)) return std::nullopt;
		if (!r.ReadBytes(&out.reason, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCinematicPlayNotificationPayload(uint32_t sequenceId, uint8_t reason)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(sequenceId)) return {};
		if (!w.WriteBytes(&reason, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildCinematicPlayNotificationPacket(uint32_t sequenceId, uint8_t reason,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU32(sequenceId)) return {};
		if (!w.WriteBytes(&reason, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeCinematicPlayNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// CINEMATIC_ACK — Request (109)
	// -------------------------------------------------------------------------

	std::optional<CinematicAckRequestPayload> ParseCinematicAckRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 sequenceId (4) + uint8 completionState (1) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		CinematicAckRequestPayload out;
		if (!r.ReadU32(out.sequenceId)) return std::nullopt;
		if (!r.ReadBytes(&out.completionState, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCinematicAckRequestPayload(uint32_t sequenceId, uint8_t completionState)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(sequenceId)) return {};
		if (!w.WriteBytes(&completionState, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// CINEMATIC_ACK — Response (110)
	// -------------------------------------------------------------------------

	std::optional<CinematicAckResponsePayload> ParseCinematicAckResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		CinematicAckResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCinematicAckResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildCinematicAckResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCinematicAckResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// CINEMATIC_SKIP — Request (111)
	// -------------------------------------------------------------------------

	std::optional<CinematicSkipRequestPayload> ParseCinematicSkipRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 sequenceId (4).
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		CinematicSkipRequestPayload out;
		if (!r.ReadU32(out.sequenceId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildCinematicSkipRequestPayload(uint32_t sequenceId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(sequenceId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// CINEMATIC_SKIP — Response (112)
	// -------------------------------------------------------------------------

	std::optional<CinematicSkipResponsePayload> ParseCinematicSkipResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1) + uint8 allowed (1) = 2.
		if (!payload || payloadSize < 2u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		CinematicSkipResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		uint8_t allowedByte = 0;
		if (!r.ReadBytes(&allowedByte, 1u)) return std::nullopt;
		out.allowed = (allowedByte != 0u);
		return out;
	}

	std::vector<uint8_t> BuildCinematicSkipResponsePayload(uint8_t error, bool allowed)
	{
		std::vector<uint8_t> buf(2u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		const uint8_t allowedByte = allowed ? 1u : 0u;
		if (!w.WriteBytes(&allowedByte, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildCinematicSkipResponsePacket(uint8_t error, bool allowed,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const uint8_t allowedByte = allowed ? 1u : 0u;
		if (!w.WriteBytes(&allowedByte, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeCinematicSkipResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
