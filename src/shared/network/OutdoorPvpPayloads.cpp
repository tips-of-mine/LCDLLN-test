// CMANGOS.36 (Phase 5.36 step 3+4) — Implementation Parse/Build des payloads OutdoorPvP.
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

#include "src/shared/network/OutdoorPvpPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit un OutdoorPvpObjectiveSummary (uint32 objectiveId, uint8 owner,
		/// uint32 capturePct, uint8 capturingBy).
		bool WriteObjectiveSummary(ByteWriter& w, const OutdoorPvpObjectiveSummary& obj)
		{
			if (!w.WriteU32(obj.objectiveId))             return false;
			if (!w.WriteBytes(&obj.owner, 1u))            return false;
			if (!w.WriteU32(obj.capturePct))              return false;
			if (!w.WriteBytes(&obj.capturingBy, 1u))      return false;
			return true;
		}

		/// Lit un OutdoorPvpObjectiveSummary.
		bool ReadObjectiveSummary(ByteReader& r, OutdoorPvpObjectiveSummary& out)
		{
			if (!r.ReadU32(out.objectiveId))              return false;
			uint8_t ownerByte = 0;
			if (!r.ReadBytes(&ownerByte, 1u))             return false;
			out.owner = ownerByte;
			if (!r.ReadU32(out.capturePct))               return false;
			uint8_t capByByte = 0;
			if (!r.ReadBytes(&capByByte, 1u))             return false;
			out.capturingBy = capByByte;
			return true;
		}

		/// Ecrit un OutdoorPvpZoneSummary (zoneId, name, scores, objectives[]).
		bool WriteZoneSummary(ByteWriter& w, const OutdoorPvpZoneSummary& zone)
		{
			if (!w.WriteU32(zone.zoneId))                 return false;
			if (!w.WriteString(zone.name))                return false;
			if (!w.WriteU32(zone.allianceScore))          return false;
			if (!w.WriteU32(zone.hordeScore))             return false;
			if (!w.WriteArrayCount(static_cast<uint16_t>(zone.objectives.size()))) return false;
			for (const auto& obj : zone.objectives)
			{
				if (!WriteObjectiveSummary(w, obj))       return false;
			}
			return true;
		}

		/// Lit un OutdoorPvpZoneSummary.
		bool ReadZoneSummary(ByteReader& r, OutdoorPvpZoneSummary& out)
		{
			if (!r.ReadU32(out.zoneId))                   return false;
			if (!r.ReadString(out.name))                  return false;
			if (!r.ReadU32(out.allianceScore))            return false;
			if (!r.ReadU32(out.hordeScore))               return false;
			uint16_t objCount = 0;
			if (!r.ReadArrayCount(objCount))              return false;
			out.objectives.reserve(static_cast<size_t>(objCount));
			for (uint16_t i = 0; i < objCount; ++i)
			{
				OutdoorPvpObjectiveSummary obj;
				if (!ReadObjectiveSummary(r, obj))        return false;
				out.objectives.push_back(std::move(obj));
			}
			return true;
		}

		/// Serialise le body d'un CAPTURE_PROGRESS_NOTIFICATION (zoneId,
		/// objectiveId, capturePct, capturingBy).
		bool WriteCaptureProgressBody(ByteWriter& w, uint32_t zoneId, uint32_t objectiveId,
			uint32_t capturePct, uint8_t capturingBy)
		{
			if (!w.WriteU32(zoneId))                      return false;
			if (!w.WriteU32(objectiveId))                 return false;
			if (!w.WriteU32(capturePct))                  return false;
			if (!w.WriteBytes(&capturingBy, 1u))          return false;
			return true;
		}

		/// Serialise le body d'un CAPTURE_COMPLETED_NOTIFICATION (zoneId,
		/// objectiveId, newOwner, allianceScore, hordeScore).
		bool WriteCaptureCompletedBody(ByteWriter& w, uint32_t zoneId, uint32_t objectiveId,
			uint8_t newOwner, uint32_t allianceScore, uint32_t hordeScore)
		{
			if (!w.WriteU32(zoneId))                      return false;
			if (!w.WriteU32(objectiveId))                 return false;
			if (!w.WriteBytes(&newOwner, 1u))             return false;
			if (!w.WriteU32(allianceScore))               return false;
			if (!w.WriteU32(hordeScore))                  return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_ZONE_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpZoneListRequestPayload> ParseOutdoorPvpZoneListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return OutdoorPvpZoneListRequestPayload{};
	}

	std::vector<uint8_t> BuildOutdoorPvpZoneListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_ZONE_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpZoneListResponsePayload> ParseOutdoorPvpZoneListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpZoneListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.zones.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			OutdoorPvpZoneSummary z;
			if (!ReadZoneSummary(r, z)) return std::nullopt;
			out.zones.push_back(std::move(z));
		}
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpZoneListResponsePayload(uint8_t error, const std::vector<OutdoorPvpZoneSummary>& zones)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(zones.size()))) return {};
			for (const auto& z : zones)
			{
				if (!WriteZoneSummary(w, z)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpZoneListResponsePacket(uint8_t error, const std::vector<OutdoorPvpZoneSummary>& zones,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(zones.size()))) return {};
			for (const auto& z : zones)
			{
				if (!WriteZoneSummary(w, z)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpZoneListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_SUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpSubscribeRequestPayload> ParseOutdoorPvpSubscribeRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpSubscribeRequestPayload out;
		if (!r.ReadU32(out.zoneId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpSubscribeRequestPayload(uint32_t zoneId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(zoneId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_SUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpSubscribeResponsePayload> ParseOutdoorPvpSubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpSubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpSubscribeResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpSubscribeResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpSubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_UNSUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpUnsubscribeRequestPayload> ParseOutdoorPvpUnsubscribeRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpUnsubscribeRequestPayload out;
		if (!r.ReadU32(out.zoneId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeRequestPayload(uint32_t zoneId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(zoneId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_UNSUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpUnsubscribeResponsePayload> ParseOutdoorPvpUnsubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpUnsubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpUnsubscribeResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpUnsubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_CAPTURE_START — Request
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpCaptureStartRequestPayload> ParseOutdoorPvpCaptureStartRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 zoneId (4) + uint32 objectiveId (4) + uint8 faction (1) = 9.
		if (!payload || payloadSize < 9u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpCaptureStartRequestPayload out;
		if (!r.ReadU32(out.zoneId))      return std::nullopt;
		if (!r.ReadU32(out.objectiveId)) return std::nullopt;
		uint8_t factionByte = 0;
		if (!r.ReadBytes(&factionByte, 1u)) return std::nullopt;
		out.faction = factionByte;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureStartRequestPayload(uint32_t zoneId, uint32_t objectiveId, uint8_t faction)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(zoneId))           return {};
		if (!w.WriteU32(objectiveId))      return {};
		if (!w.WriteBytes(&faction, 1u))   return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_CAPTURE_START — Response
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpCaptureStartResponsePayload> ParseOutdoorPvpCaptureStartResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpCaptureStartResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureStartResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureStartResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpCaptureStartResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpCaptureProgressNotificationPayload> ParseOutdoorPvpCaptureProgressNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 zoneId (4) + uint32 objectiveId (4) + uint32 capturePct (4)
		// + uint8 capturingBy (1) = 13.
		if (!payload || payloadSize < 13u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpCaptureProgressNotificationPayload out;
		if (!r.ReadU32(out.zoneId))      return std::nullopt;
		if (!r.ReadU32(out.objectiveId)) return std::nullopt;
		if (!r.ReadU32(out.capturePct))  return std::nullopt;
		uint8_t capByByte = 0;
		if (!r.ReadBytes(&capByByte, 1u)) return std::nullopt;
		out.capturingBy = capByByte;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureProgressNotificationPayload(uint32_t zoneId, uint32_t objectiveId,
		uint32_t capturePct, uint8_t capturingBy)
	{
		std::vector<uint8_t> buf(13u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteCaptureProgressBody(w, zoneId, objectiveId, capturePct, capturingBy)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureProgressNotificationPacket(uint32_t zoneId, uint32_t objectiveId,
		uint32_t capturePct, uint8_t capturingBy, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteCaptureProgressBody(w, zoneId, objectiveId, capturePct, capturingBy)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpCaptureProgressNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<OutdoorPvpCaptureCompletedNotificationPayload> ParseOutdoorPvpCaptureCompletedNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 zoneId (4) + uint32 objectiveId (4) + uint8 newOwner (1)
		// + uint32 allianceScore (4) + uint32 hordeScore (4) = 17.
		if (!payload || payloadSize < 17u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		OutdoorPvpCaptureCompletedNotificationPayload out;
		if (!r.ReadU32(out.zoneId))         return std::nullopt;
		if (!r.ReadU32(out.objectiveId))    return std::nullopt;
		uint8_t ownerByte = 0;
		if (!r.ReadBytes(&ownerByte, 1u))   return std::nullopt;
		out.newOwner = ownerByte;
		if (!r.ReadU32(out.allianceScore))  return std::nullopt;
		if (!r.ReadU32(out.hordeScore))     return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureCompletedNotificationPayload(uint32_t zoneId, uint32_t objectiveId,
		uint8_t newOwner, uint32_t allianceScore, uint32_t hordeScore)
	{
		std::vector<uint8_t> buf(17u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteCaptureCompletedBody(w, zoneId, objectiveId, newOwner, allianceScore, hordeScore)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildOutdoorPvpCaptureCompletedNotificationPacket(uint32_t zoneId, uint32_t objectiveId,
		uint8_t newOwner, uint32_t allianceScore, uint32_t hordeScore, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteCaptureCompletedBody(w, zoneId, objectiveId, newOwner, allianceScore, hordeScore)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeOutdoorPvpCaptureCompletedNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
