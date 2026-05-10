// CMANGOS.33 (Phase 5.33 step 3+4) — Implementation Parse/Build des payloads Lfg.
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
// Note : ByteWriter/ByteReader serialise les bool sur 1 octet via
// WriteBytes / ReadBytes pour preserver la portabilite (pas de
// WriteBool/ReadBool dispo).

#include "src/shared/network/LfgPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit un membre (uint64 accountId, uint8 role).
		bool WriteMember(ByteWriter& w, const LfgMatchMember& m)
		{
			if (!w.WriteU64(m.accountId)) return false;
			if (!w.WriteBytes(&m.role, 1u)) return false;
			return true;
		}

		/// Lit un membre (uint64 accountId, uint8 role).
		bool ReadMember(ByteReader& r, LfgMatchMember& out)
		{
			if (!r.ReadU64(out.accountId)) return false;
			uint8_t role = 0;
			if (!r.ReadBytes(&role, 1u)) return false;
			out.role = role;
			return true;
		}

		/// Serialise le body d'un MATCH_PROPOSAL_NOTIFICATION.
		bool WriteMatchProposalBody(ByteWriter& w, uint64_t proposalId, uint32_t dungeonId,
			const std::vector<LfgMatchMember>& members)
		{
			if (!w.WriteU64(proposalId))                                    return false;
			if (!w.WriteU32(dungeonId))                                     return false;
			if (!w.WriteArrayCount(static_cast<uint16_t>(members.size())))  return false;
			for (const auto& m : members)
			{
				if (!WriteMember(w, m)) return false;
			}
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// LFG_QUEUE — Request
	// -------------------------------------------------------------------------

	std::optional<LfgQueueRequestPayload> ParseLfgQueueRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 role (1) + uint32 dungeonId (4) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgQueueRequestPayload out;
		uint8_t role = 0;
		if (!r.ReadBytes(&role, 1u)) return std::nullopt;
		out.role = role;
		if (!r.ReadU32(out.dungeonId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLfgQueueRequestPayload(uint8_t role, uint32_t dungeonId)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&role, 1u)) return {};
		if (!w.WriteU32(dungeonId))   return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// LFG_QUEUE — Response
	// -------------------------------------------------------------------------

	std::optional<LfgQueueResponsePayload> ParseLfgQueueResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgQueueResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		// Si error == 0, lire estimatedWaitSec.
		if (!r.ReadU32(out.estimatedWaitSec)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLfgQueueResponsePayload(uint8_t error, uint32_t estimatedWaitSec)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU32(estimatedWaitSec)) return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLfgQueueResponsePacket(uint8_t error, uint32_t estimatedWaitSec,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU32(estimatedWaitSec)) return {};
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLfgQueueResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LFG_LEAVE — Request
	// -------------------------------------------------------------------------

	std::optional<LfgLeaveRequestPayload> ParseLfgLeaveRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return LfgLeaveRequestPayload{};
	}

	std::vector<uint8_t> BuildLfgLeaveRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// LFG_LEAVE — Response
	// -------------------------------------------------------------------------

	std::optional<LfgLeaveResponsePayload> ParseLfgLeaveResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgLeaveResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLfgLeaveResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildLfgLeaveResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLfgLeaveResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LFG_STATUS — Request
	// -------------------------------------------------------------------------

	std::optional<LfgStatusRequestPayload> ParseLfgStatusRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		return LfgStatusRequestPayload{};
	}

	std::vector<uint8_t> BuildLfgStatusRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// LFG_STATUS — Response
	// -------------------------------------------------------------------------

	std::optional<LfgStatusResponsePayload> ParseLfgStatusResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1) + uint8 inQueue (1) + uint8 role (1) + uint32 dungeonId (4) + uint32 elapsedSec (4) = 11.
		if (!payload || payloadSize < 11u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgStatusResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		uint8_t inQueueByte = 0;
		if (!r.ReadBytes(&inQueueByte, 1u)) return std::nullopt;
		out.inQueue = (inQueueByte != 0u);
		uint8_t roleByte = 0;
		if (!r.ReadBytes(&roleByte, 1u)) return std::nullopt;
		out.role = roleByte;
		if (!r.ReadU32(out.dungeonId))  return std::nullopt;
		if (!r.ReadU32(out.elapsedSec)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLfgStatusResponsePayload(uint8_t error, bool inQueue, uint8_t role, uint32_t dungeonId, uint32_t elapsedSec)
	{
		std::vector<uint8_t> buf(11u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		const uint8_t inQueueByte = inQueue ? 1u : 0u;
		if (!w.WriteBytes(&inQueueByte, 1u)) return {};
		if (!w.WriteBytes(&role, 1u))        return {};
		if (!w.WriteU32(dungeonId))          return {};
		if (!w.WriteU32(elapsedSec))         return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLfgStatusResponsePacket(uint8_t error, bool inQueue, uint8_t role, uint32_t dungeonId, uint32_t elapsedSec,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const uint8_t inQueueByte = inQueue ? 1u : 0u;
		if (!w.WriteBytes(&inQueueByte, 1u)) return {};
		if (!w.WriteBytes(&role, 1u))        return {};
		if (!w.WriteU32(dungeonId))          return {};
		if (!w.WriteU32(elapsedSec))         return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLfgStatusResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LFG_MATCH_PROPOSAL_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<LfgMatchProposalNotificationPayload> ParseLfgMatchProposalNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 proposalId (8) + uint32 dungeonId (4) + uint16 count (2) = 14.
		if (!payload || payloadSize < 14u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgMatchProposalNotificationPayload out;
		if (!r.ReadU64(out.proposalId)) return std::nullopt;
		if (!r.ReadU32(out.dungeonId))  return std::nullopt;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))   return std::nullopt;
		out.members.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			LfgMatchMember m;
			if (!ReadMember(r, m)) return std::nullopt;
			out.members.push_back(m);
		}
		return out;
	}

	std::vector<uint8_t> BuildLfgMatchProposalNotificationPayload(uint64_t proposalId, uint32_t dungeonId,
		const std::vector<LfgMatchMember>& members)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteMatchProposalBody(w, proposalId, dungeonId, members)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLfgMatchProposalNotificationPacket(uint64_t proposalId, uint32_t dungeonId,
		const std::vector<LfgMatchMember>& members, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteMatchProposalBody(w, proposalId, dungeonId, members)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeLfgMatchProposalNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LFG_MATCH_ACCEPT — Request
	// -------------------------------------------------------------------------

	std::optional<LfgMatchAcceptRequestPayload> ParseLfgMatchAcceptRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 proposalId (8) + uint8 accept (1) = 9.
		if (!payload || payloadSize < 9u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LfgMatchAcceptRequestPayload out;
		if (!r.ReadU64(out.proposalId)) return std::nullopt;
		uint8_t acceptByte = 0;
		if (!r.ReadBytes(&acceptByte, 1u)) return std::nullopt;
		out.accept = (acceptByte != 0u);
		return out;
	}

	std::vector<uint8_t> BuildLfgMatchAcceptRequestPayload(uint64_t proposalId, bool accept)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(proposalId)) return {};
		const uint8_t acceptByte = accept ? 1u : 0u;
		if (!w.WriteBytes(&acceptByte, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}
}
