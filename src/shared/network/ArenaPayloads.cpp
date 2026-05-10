// CMANGOS.21 (Phase 5.21 step 3+4) — Implementation Parse/Build des payloads Arena.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket utilise PacketBuilder pour assembler le paquet
//     complet header + payload, pret a passer a NetServer::Send.
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/ArenaPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit un ArenaTeamSummary (uint32 teamId, uint8 size, string name,
		/// uint32 rating, uint32 weeklyGames, uint32 weeklyWins).
		bool WriteTeamSummary(ByteWriter& w, const ArenaTeamSummary& t)
		{
			if (!w.WriteU32(t.teamId))                  return false;
			if (!w.WriteBytes(&t.size, 1u))             return false;
			if (!w.WriteString(t.name))                 return false;
			if (!w.WriteU32(t.rating))                  return false;
			if (!w.WriteU32(t.weeklyGames))             return false;
			if (!w.WriteU32(t.weeklyWins))              return false;
			return true;
		}

		/// Lit un ArenaTeamSummary.
		bool ReadTeamSummary(ByteReader& r, ArenaTeamSummary& out)
		{
			if (!r.ReadU32(out.teamId))                 return false;
			uint8_t sizeByte = 0;
			if (!r.ReadBytes(&sizeByte, 1u))            return false;
			out.size = sizeByte;
			if (!r.ReadString(out.name))                return false;
			if (!r.ReadU32(out.rating))                 return false;
			if (!r.ReadU32(out.weeklyGames))            return false;
			if (!r.ReadU32(out.weeklyWins))             return false;
			return true;
		}

		/// Serialise le body d'un MATCH_PROPOSAL_NOTIFICATION (proposalId,
		/// opponentTeamName, opponentRating).
		bool WriteMatchProposalBody(ByteWriter& w, uint32_t proposalId,
			const std::string& opponentTeamName, uint32_t opponentRating)
		{
			if (!w.WriteU32(proposalId))                return false;
			if (!w.WriteString(opponentTeamName))       return false;
			if (!w.WriteU32(opponentRating))            return false;
			return true;
		}

		/// Serialise le body d'un MATCH_RESULT_NOTIFICATION (win, oldRating,
		/// newRating, opponentName).
		bool WriteMatchResultBody(ByteWriter& w, bool win, uint32_t oldRating,
			uint32_t newRating, const std::string& opponentName)
		{
			const uint8_t winByte = win ? 1u : 0u;
			if (!w.WriteBytes(&winByte, 1u))            return false;
			if (!w.WriteU32(oldRating))                 return false;
			if (!w.WriteU32(newRating))                 return false;
			if (!w.WriteString(opponentName))           return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// ARENA_TEAM_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<ArenaTeamListRequestPayload> ParseArenaTeamListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return ArenaTeamListRequestPayload{};
	}

	std::vector<uint8_t> BuildArenaTeamListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// ARENA_TEAM_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<ArenaTeamListResponsePayload> ParseArenaTeamListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaTeamListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		// Si error == 0, lire count + entries.
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.teams.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			ArenaTeamSummary t;
			if (!ReadTeamSummary(r, t)) return std::nullopt;
			out.teams.push_back(std::move(t));
		}
		return out;
	}

	std::vector<uint8_t> BuildArenaTeamListResponsePayload(uint8_t error, const std::vector<ArenaTeamSummary>& teams)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(teams.size()))) return {};
			for (const auto& t : teams)
			{
				if (!WriteTeamSummary(w, t)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildArenaTeamListResponsePacket(uint8_t error, const std::vector<ArenaTeamSummary>& teams,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(teams.size()))) return {};
			for (const auto& t : teams)
			{
				if (!WriteTeamSummary(w, t)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeArenaTeamListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// ARENA_QUEUE — Request
	// -------------------------------------------------------------------------

	std::optional<ArenaQueueRequestPayload> ParseArenaQueueRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 teamId (4) + uint8 size (1) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaQueueRequestPayload out;
		if (!r.ReadU32(out.teamId)) return std::nullopt;
		uint8_t sizeByte = 0;
		if (!r.ReadBytes(&sizeByte, 1u)) return std::nullopt;
		out.size = sizeByte;
		return out;
	}

	std::vector<uint8_t> BuildArenaQueueRequestPayload(uint32_t teamId, uint8_t size)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(teamId))         return {};
		if (!w.WriteBytes(&size, 1u))    return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// ARENA_QUEUE — Response
	// -------------------------------------------------------------------------

	std::optional<ArenaQueueResponsePayload> ParseArenaQueueResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaQueueResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		if (!r.ReadU32(out.estimatedWaitSec)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildArenaQueueResponsePayload(uint8_t error, uint32_t estimatedWaitSec)
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

	std::vector<uint8_t> BuildArenaQueueResponsePacket(uint8_t error, uint32_t estimatedWaitSec,
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
		if (!builder.Finalize(kOpcodeArenaQueueResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// ARENA_LEAVE_QUEUE — Request
	// -------------------------------------------------------------------------

	std::optional<ArenaLeaveQueueRequestPayload> ParseArenaLeaveQueueRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		return ArenaLeaveQueueRequestPayload{};
	}

	std::vector<uint8_t> BuildArenaLeaveQueueRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// ARENA_LEAVE_QUEUE — Response
	// -------------------------------------------------------------------------

	std::optional<ArenaLeaveQueueResponsePayload> ParseArenaLeaveQueueResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaLeaveQueueResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildArenaLeaveQueueResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildArenaLeaveQueueResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeArenaLeaveQueueResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// ARENA_MATCH_PROPOSAL_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<ArenaMatchProposalNotificationPayload> ParseArenaMatchProposalNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 proposalId (4) + uint16 strLen (2) + uint32 rating (4) = 10.
		if (!payload || payloadSize < 10u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaMatchProposalNotificationPayload out;
		if (!r.ReadU32(out.proposalId))           return std::nullopt;
		if (!r.ReadString(out.opponentTeamName))  return std::nullopt;
		if (!r.ReadU32(out.opponentRating))       return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildArenaMatchProposalNotificationPayload(uint32_t proposalId,
		const std::string& opponentTeamName, uint32_t opponentRating)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteMatchProposalBody(w, proposalId, opponentTeamName, opponentRating)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildArenaMatchProposalNotificationPacket(uint32_t proposalId,
		const std::string& opponentTeamName, uint32_t opponentRating, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteMatchProposalBody(w, proposalId, opponentTeamName, opponentRating)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeArenaMatchProposalNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// ARENA_MATCH_ACCEPT — Request
	// -------------------------------------------------------------------------

	std::optional<ArenaMatchAcceptRequestPayload> ParseArenaMatchAcceptRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 proposalId (4) + uint8 accept (1) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaMatchAcceptRequestPayload out;
		if (!r.ReadU32(out.proposalId)) return std::nullopt;
		uint8_t acceptByte = 0;
		if (!r.ReadBytes(&acceptByte, 1u)) return std::nullopt;
		out.accept = (acceptByte != 0u);
		return out;
	}

	std::vector<uint8_t> BuildArenaMatchAcceptRequestPayload(uint32_t proposalId, bool accept)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(proposalId))         return {};
		const uint8_t acceptByte = accept ? 1u : 0u;
		if (!w.WriteBytes(&acceptByte, 1u))  return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// ARENA_MATCH_ACCEPT — Response
	// -------------------------------------------------------------------------

	std::optional<ArenaMatchAcceptResponsePayload> ParseArenaMatchAcceptResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaMatchAcceptResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildArenaMatchAcceptResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildArenaMatchAcceptResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeArenaMatchAcceptResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// ARENA_MATCH_RESULT_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<ArenaMatchResultNotificationPayload> ParseArenaMatchResultNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 win (1) + uint32 oldRating (4) + uint32 newRating (4) + uint16 strLen (2) = 11.
		if (!payload || payloadSize < 11u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		ArenaMatchResultNotificationPayload out;
		uint8_t winByte = 0;
		if (!r.ReadBytes(&winByte, 1u))      return std::nullopt;
		out.win = (winByte != 0u);
		if (!r.ReadU32(out.oldRating))       return std::nullopt;
		if (!r.ReadU32(out.newRating))       return std::nullopt;
		if (!r.ReadString(out.opponentName)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildArenaMatchResultNotificationPayload(bool win, uint32_t oldRating,
		uint32_t newRating, const std::string& opponentName)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteMatchResultBody(w, win, oldRating, newRating, opponentName)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildArenaMatchResultNotificationPacket(bool win, uint32_t oldRating,
		uint32_t newRating, const std::string& opponentName, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteMatchResultBody(w, win, oldRating, newRating, opponentName)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeArenaMatchResultNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
