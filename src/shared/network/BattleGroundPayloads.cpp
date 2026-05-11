// CMANGOS.10 (Phase 5 step 3+4) — Implementation Parse/Build des payloads BG.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket / Build*NotificationPacket utilise PacketBuilder
//     pour assembler le paquet complet header + payload, pret a passer a
//     NetServer::Send.
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/BattleGroundPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit un BgInfo (uint16 bgType, string name, uint8 teamSize, string mapName).
		bool WriteBgInfo(ByteWriter& w, const BgInfo& bg)
		{
			if (!w.WriteU16(bg.bgType))                   return false;
			if (!w.WriteString(bg.name))                  return false;
			if (!w.WriteBytes(&bg.teamSize, 1u))          return false;
			if (!w.WriteString(bg.mapName))               return false;
			return true;
		}

		/// Lit un BgInfo.
		bool ReadBgInfo(ByteReader& r, BgInfo& out)
		{
			if (!r.ReadU16(out.bgType))                   return false;
			if (!r.ReadString(out.name))                  return false;
			uint8_t teamSizeByte = 0;
			if (!r.ReadBytes(&teamSizeByte, 1u))          return false;
			out.teamSize = teamSizeByte;
			if (!r.ReadString(out.mapName))               return false;
			return true;
		}

		/// Serialise le body d'un MATCH_START_NOTIFICATION (matchId, bgType,
		/// mapName, allianceCount, hordeCount).
		bool WriteMatchStartBody(ByteWriter& w, uint64_t matchId, uint16_t bgType,
			const std::string& mapName, uint8_t allianceCount, uint8_t hordeCount)
		{
			if (!w.WriteU64(matchId))                     return false;
			if (!w.WriteU16(bgType))                      return false;
			if (!w.WriteString(mapName))                  return false;
			if (!w.WriteBytes(&allianceCount, 1u))        return false;
			if (!w.WriteBytes(&hordeCount, 1u))           return false;
			return true;
		}

		/// Serialise le body d'un SCORE_UPDATE_NOTIFICATION (matchId, scores, elapsed).
		bool WriteScoreUpdateBody(ByteWriter& w, uint64_t matchId,
			uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec)
		{
			if (!w.WriteU64(matchId))                     return false;
			if (!w.WriteU32(allianceScore))               return false;
			if (!w.WriteU32(hordeScore))                  return false;
			if (!w.WriteU32(elapsedSec))                  return false;
			return true;
		}

		/// Serialise le body d'un MATCH_END_NOTIFICATION (matchId, winnerFaction,
		/// scores, durationSec).
		bool WriteMatchEndBody(ByteWriter& w, uint64_t matchId, uint8_t winnerFaction,
			uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec)
		{
			if (!w.WriteU64(matchId))                     return false;
			if (!w.WriteBytes(&winnerFaction, 1u))        return false;
			if (!w.WriteU32(allianceScore))               return false;
			if (!w.WriteU32(hordeScore))                  return false;
			if (!w.WriteU32(durationSec))                 return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// BG_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<BgListRequestPayload> ParseBgListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return BgListRequestPayload{};
	}

	std::vector<uint8_t> BuildBgListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// BG_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<BgListResponsePayload> ParseBgListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		// Si error == 0, lire count + entries.
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.battlegrounds.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			BgInfo bg;
			if (!ReadBgInfo(r, bg)) return std::nullopt;
			out.battlegrounds.push_back(std::move(bg));
		}
		return out;
	}

	std::vector<uint8_t> BuildBgListResponsePayload(uint8_t error, const std::vector<BgInfo>& battlegrounds)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(battlegrounds.size()))) return {};
			for (const auto& bg : battlegrounds)
			{
				if (!WriteBgInfo(w, bg)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildBgListResponsePacket(uint8_t error, const std::vector<BgInfo>& battlegrounds,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(battlegrounds.size()))) return {};
			for (const auto& bg : battlegrounds)
			{
				if (!WriteBgInfo(w, bg)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeBgListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_QUEUE — Request
	// -------------------------------------------------------------------------

	std::optional<BgQueueRequestPayload> ParseBgQueueRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint16 bgType (2) + uint8 faction (1) = 3.
		if (!payload || payloadSize < 3u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgQueueRequestPayload out;
		if (!r.ReadU16(out.bgType)) return std::nullopt;
		uint8_t factionByte = 0;
		if (!r.ReadBytes(&factionByte, 1u)) return std::nullopt;
		out.faction = factionByte;
		return out;
	}

	std::vector<uint8_t> BuildBgQueueRequestPayload(uint16_t bgType, uint8_t faction)
	{
		std::vector<uint8_t> buf(3u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU16(bgType))           return {};
		if (!w.WriteBytes(&faction, 1u))   return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// BG_QUEUE — Response
	// -------------------------------------------------------------------------

	std::optional<BgQueueResponsePayload> ParseBgQueueResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgQueueResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		if (!r.ReadU32(out.estimatedWaitSec)) return std::nullopt;
		if (!r.ReadU32(out.queuePosition))    return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildBgQueueResponsePayload(uint8_t error, uint32_t estimatedWaitSec, uint32_t queuePosition)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU32(estimatedWaitSec)) return {};
			if (!w.WriteU32(queuePosition))    return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildBgQueueResponsePacket(uint8_t error, uint32_t estimatedWaitSec, uint32_t queuePosition,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU32(estimatedWaitSec)) return {};
			if (!w.WriteU32(queuePosition))    return {};
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeBgQueueResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_LEAVE_QUEUE — Request
	// -------------------------------------------------------------------------

	std::optional<BgLeaveQueueRequestPayload> ParseBgLeaveQueueRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		return BgLeaveQueueRequestPayload{};
	}

	std::vector<uint8_t> BuildBgLeaveQueueRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// BG_LEAVE_QUEUE — Response
	// -------------------------------------------------------------------------

	std::optional<BgLeaveQueueResponsePayload> ParseBgLeaveQueueResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgLeaveQueueResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildBgLeaveQueueResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildBgLeaveQueueResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeBgLeaveQueueResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_MATCH_START_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<BgMatchStartNotificationPayload> ParseBgMatchStartNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 matchId (8) + uint16 bgType (2) + uint16 strLen (2)
		// + uint8 allianceCount (1) + uint8 hordeCount (1) = 14.
		if (!payload || payloadSize < 14u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgMatchStartNotificationPayload out;
		if (!r.ReadU64(out.matchId))             return std::nullopt;
		if (!r.ReadU16(out.bgType))              return std::nullopt;
		if (!r.ReadString(out.mapName))          return std::nullopt;
		uint8_t allianceByte = 0;
		if (!r.ReadBytes(&allianceByte, 1u))     return std::nullopt;
		out.allianceCount = allianceByte;
		uint8_t hordeByte = 0;
		if (!r.ReadBytes(&hordeByte, 1u))        return std::nullopt;
		out.hordeCount = hordeByte;
		return out;
	}

	std::vector<uint8_t> BuildBgMatchStartNotificationPayload(uint64_t matchId, uint16_t bgType,
		const std::string& mapName, uint8_t allianceCount, uint8_t hordeCount)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteMatchStartBody(w, matchId, bgType, mapName, allianceCount, hordeCount)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildBgMatchStartNotificationPacket(uint64_t matchId, uint16_t bgType,
		const std::string& mapName, uint8_t allianceCount, uint8_t hordeCount, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteMatchStartBody(w, matchId, bgType, mapName, allianceCount, hordeCount)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeBgMatchStartNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_SCORE_UPDATE_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<BgScoreUpdateNotificationPayload> ParseBgScoreUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 matchId (8) + uint32 allianceScore (4) + uint32 hordeScore (4)
		// + uint32 elapsedSec (4) = 20.
		if (!payload || payloadSize < 20u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgScoreUpdateNotificationPayload out;
		if (!r.ReadU64(out.matchId))         return std::nullopt;
		if (!r.ReadU32(out.allianceScore))   return std::nullopt;
		if (!r.ReadU32(out.hordeScore))      return std::nullopt;
		if (!r.ReadU32(out.elapsedSec))      return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildBgScoreUpdateNotificationPayload(uint64_t matchId,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec)
	{
		std::vector<uint8_t> buf(20u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteScoreUpdateBody(w, matchId, allianceScore, hordeScore, elapsedSec)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildBgScoreUpdateNotificationPacket(uint64_t matchId,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t elapsedSec, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteScoreUpdateBody(w, matchId, allianceScore, hordeScore, elapsedSec)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeBgScoreUpdateNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_MATCH_END_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<BgMatchEndNotificationPayload> ParseBgMatchEndNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 matchId (8) + uint8 winnerFaction (1) + uint32 alliance (4)
		// + uint32 horde (4) + uint32 duration (4) = 21.
		if (!payload || payloadSize < 21u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		BgMatchEndNotificationPayload out;
		if (!r.ReadU64(out.matchId))        return std::nullopt;
		uint8_t winnerByte = 0;
		if (!r.ReadBytes(&winnerByte, 1u))  return std::nullopt;
		out.winnerFaction = winnerByte;
		if (!r.ReadU32(out.allianceScore))  return std::nullopt;
		if (!r.ReadU32(out.hordeScore))     return std::nullopt;
		if (!r.ReadU32(out.durationSec))    return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildBgMatchEndNotificationPayload(uint64_t matchId, uint8_t winnerFaction,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec)
	{
		std::vector<uint8_t> buf(21u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteMatchEndBody(w, matchId, winnerFaction, allianceScore, hordeScore, durationSec)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildBgMatchEndNotificationPacket(uint64_t matchId, uint8_t winnerFaction,
		uint32_t allianceScore, uint32_t hordeScore, uint32_t durationSec, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteMatchEndBody(w, matchId, winnerFaction, allianceScore, hordeScore, durationSec)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeBgMatchEndNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// BG_LEAVE_MATCH — Request (fire-and-forget V1)
	// -------------------------------------------------------------------------

	std::optional<BgLeaveMatchRequestPayload> ParseBgLeaveMatchRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		return BgLeaveMatchRequestPayload{};
	}

	std::vector<uint8_t> BuildBgLeaveMatchRequestPayload()
	{
		return std::vector<uint8_t>{};
	}
}
