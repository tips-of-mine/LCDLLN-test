// CMANGOS.39 (Phase 4.39 step 3+4) — Implementation Parse/Build des payloads Skills.
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
// Note : ByteWriter expose WriteU16, on l'utilise donc pour les champs uint16
// (skillId, value, cap, bonus, delta cast). Le delta du push UpgradeNotification
// est int16 ; on le serialise via static_cast<uint16_t> et on re-cast en int16
// au Parse (pattern bit-pour-bit two's complement).

#include "src/shared/network/SkillPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit une entree (uint16 skillId, uint16 value, uint16 cap, uint16 bonus).
		bool WriteEntry(ByteWriter& w, const SkillBookEntry& e)
		{
			if (!w.WriteU16(e.skillId)) return false;
			if (!w.WriteU16(e.value))   return false;
			if (!w.WriteU16(e.cap))     return false;
			if (!w.WriteU16(e.bonus))   return false;
			return true;
		}

		/// Lit une entree (uint16 skillId, uint16 value, uint16 cap, uint16 bonus).
		bool ReadEntry(ByteReader& r, SkillBookEntry& e)
		{
			if (!r.ReadU16(e.skillId)) return false;
			if (!r.ReadU16(e.value))   return false;
			if (!r.ReadU16(e.cap))     return false;
			if (!r.ReadU16(e.bonus))   return false;
			return true;
		}

		/// Serialise la partie « apres error » de SKILLS_LIST_RESPONSE.
		bool WriteListBody(ByteWriter& w, uint8_t error, const std::vector<SkillBookEntry>& skills)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(skills.size())))
				return false;
			for (const auto& e : skills)
			{
				if (!WriteEntry(w, e))
					return false;
			}
			return true;
		}

		/// Serialise un SKILL_LEARN_RESPONSE body (uint8 error + uint16 initialCap).
		bool WriteLearnBody(ByteWriter& w, uint8_t error, uint16_t initialCap)
		{
			if (!w.WriteBytes(&error, 1u))      return false;
			if (!w.WriteU16(initialCap))        return false;
			return true;
		}

		/// Serialise un SKILL_USE_RESPONSE body (uint8 error + uint8 result + uint16 delta).
		bool WriteUseBody(ByteWriter& w, uint8_t error, uint8_t result, uint16_t deltaValue)
		{
			if (!w.WriteBytes(&error, 1u))    return false;
			if (!w.WriteBytes(&result, 1u))   return false;
			if (!w.WriteU16(deltaValue))      return false;
			return true;
		}

		/// Serialise un push UPGRADE_NOTIFICATION (skillId + newValue + newCap + delta).
		bool WriteUpgradeBody(ByteWriter& w, uint16_t skillId, uint16_t newValue, uint16_t newCap, int16_t delta)
		{
			if (!w.WriteU16(skillId))                              return false;
			if (!w.WriteU16(newValue))                             return false;
			if (!w.WriteU16(newCap))                               return false;
			if (!w.WriteU16(static_cast<uint16_t>(delta)))         return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// SKILLS_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<SkillsListRequestPayload> ParseSkillsListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return SkillsListRequestPayload{};
	}

	std::vector<uint8_t> BuildSkillsListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// SKILLS_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<SkillsListResponsePayload> ParseSkillsListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillsListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.skills.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			SkillBookEntry e;
			if (!ReadEntry(r, e))
				return std::nullopt;
			out.skills.push_back(e);
		}
		return out;
	}

	std::vector<uint8_t> BuildSkillsListResponsePayload(uint8_t error, const std::vector<SkillBookEntry>& skills)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteListBody(w, error, skills))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildSkillsListResponsePacket(uint8_t error, const std::vector<SkillBookEntry>& skills,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteListBody(w, error, skills))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeSkillsListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// SKILL_LEARN — Request / Response
	// -------------------------------------------------------------------------

	std::optional<SkillLearnRequestPayload> ParseSkillLearnRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint16 skillId (2).
		if (!payload || payloadSize < 2u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillLearnRequestPayload out;
		if (!r.ReadU16(out.skillId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildSkillLearnRequestPayload(uint16_t skillId)
	{
		std::vector<uint8_t> buf(2u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU16(skillId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<SkillLearnResponsePayload> ParseSkillLearnResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint16 initialCap (2) = 3 octets.
		if (!payload || payloadSize < 3u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillLearnResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (!r.ReadU16(out.initialCap))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildSkillLearnResponsePayload(uint8_t error, uint16_t initialCap)
	{
		std::vector<uint8_t> buf(3u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteLearnBody(w, error, initialCap))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildSkillLearnResponsePacket(uint8_t error, uint16_t initialCap,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteLearnBody(w, error, initialCap))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeSkillLearnResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// SKILL_USE — Request / Response
	// -------------------------------------------------------------------------

	std::optional<SkillUseRequestPayload> ParseSkillUseRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint16 skillId (2) + uint64 targetEntityId (8) = 10 octets.
		if (!payload || payloadSize < 10u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillUseRequestPayload out;
		if (!r.ReadU16(out.skillId))           return std::nullopt;
		if (!r.ReadU64(out.targetEntityId))    return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildSkillUseRequestPayload(uint16_t skillId, uint64_t targetEntityId)
	{
		std::vector<uint8_t> buf(10u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU16(skillId))           return {};
		if (!w.WriteU64(targetEntityId))    return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<SkillUseResponsePayload> ParseSkillUseResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint8 result (1) + uint16 deltaValue (2) = 4 octets.
		if (!payload || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillUseResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))    return std::nullopt;
		if (!r.ReadBytes(&out.result, 1u))   return std::nullopt;
		if (!r.ReadU16(out.deltaValue))      return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildSkillUseResponsePayload(uint8_t error, uint8_t result, uint16_t deltaValue)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteUseBody(w, error, result, deltaValue))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildSkillUseResponsePacket(uint8_t error, uint8_t result, uint16_t deltaValue,
	                                                  uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteUseBody(w, error, result, deltaValue))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeSkillUseResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// SKILL_UPGRADE_NOTIFICATION (push, request_id=0)
	// -------------------------------------------------------------------------

	std::optional<SkillUpgradeNotificationPayload> ParseSkillUpgradeNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint16 skillId (2) + uint16 newValue (2) + uint16 newCap (2) + int16 delta (2) = 8 octets.
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		SkillUpgradeNotificationPayload out;
		if (!r.ReadU16(out.skillId))    return std::nullopt;
		if (!r.ReadU16(out.newValue))   return std::nullopt;
		if (!r.ReadU16(out.newCap))     return std::nullopt;
		uint16_t deltaRaw = 0;
		if (!r.ReadU16(deltaRaw))       return std::nullopt;
		out.delta = static_cast<int16_t>(deltaRaw);
		return out;
	}

	std::vector<uint8_t> BuildSkillUpgradeNotificationPayload(uint16_t skillId, uint16_t newValue, uint16_t newCap, int16_t delta)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteUpgradeBody(w, skillId, newValue, newCap, delta))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildSkillUpgradeNotificationPacket(uint16_t skillId, uint16_t newValue, uint16_t newCap, int16_t delta,
	                                                          uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteUpgradeBody(w, skillId, newValue, newCap, delta))
			return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeSkillUpgradeNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
