// CMANGOS.23 (Phase 5.23 step 3+4) — Implementation Parse/Build des payloads Quest.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket utilise PacketBuilder pour assembler le paquet
//     complet header + payload, pret a passer a NetServer::Send. Le requestId
//     vient du paquet d'origine (cf. RequestResponseDispatcher).
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/QuestPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Ecrit (uint8 error, uint32 questId, uint8 newStatus). Mutualise pour les
		/// 3 paires Accept/Complete/Reward dont les responses ont la meme forme.
		bool WriteSimpleResponse(ByteWriter& w, uint8_t error, uint32_t questId, uint8_t newStatus)
		{
			if (!w.WriteBytes(&error, 1u))   return false;
			if (!w.WriteU32(questId))        return false;
			if (!w.WriteBytes(&newStatus, 1u)) return false;
			return true;
		}

		/// Lit (uint8 error, uint32 questId, uint8 newStatus). Symetrique de
		/// WriteSimpleResponse. \return false si le payload est trop court.
		bool ReadSimpleResponse(ByteReader& r, uint8_t& error, uint32_t& questId, uint8_t& newStatus)
		{
			if (!r.ReadBytes(&error, 1u))      return false;
			if (!r.ReadU32(questId))           return false;
			if (!r.ReadBytes(&newStatus, 1u))  return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// QUEST_ACCEPT
	// -------------------------------------------------------------------------

	std::optional<QuestAcceptRequestPayload> ParseQuestAcceptRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestAcceptRequestPayload out;
		if (!r.ReadU32(out.questId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestAcceptRequestPayload(uint32_t questId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(questId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<QuestAcceptResponsePayload> ParseQuestAcceptResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint32 questId (4) + uint8 status (1) = 6.
		if (!payload || payloadSize < 6u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestAcceptResponsePayload out;
		if (!ReadSimpleResponse(r, out.error, out.questId, out.newStatus))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestAcceptResponsePayload(uint8_t error, uint32_t questId, uint8_t newStatus)
	{
		std::vector<uint8_t> buf(6u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildQuestAcceptResponsePacket(uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeQuestAcceptResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// QUEST_COMPLETE
	// -------------------------------------------------------------------------

	std::optional<QuestCompleteRequestPayload> ParseQuestCompleteRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestCompleteRequestPayload out;
		if (!r.ReadU32(out.questId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestCompleteRequestPayload(uint32_t questId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(questId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<QuestCompleteResponsePayload> ParseQuestCompleteResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 6u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestCompleteResponsePayload out;
		if (!ReadSimpleResponse(r, out.error, out.questId, out.newStatus))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestCompleteResponsePayload(uint8_t error, uint32_t questId, uint8_t newStatus)
	{
		std::vector<uint8_t> buf(6u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildQuestCompleteResponsePacket(uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                      uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeQuestCompleteResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// QUEST_REWARD
	// -------------------------------------------------------------------------

	std::optional<QuestRewardRequestPayload> ParseQuestRewardRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestRewardRequestPayload out;
		if (!r.ReadU32(out.questId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestRewardRequestPayload(uint32_t questId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(questId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<QuestRewardResponsePayload> ParseQuestRewardResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 6u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestRewardResponsePayload out;
		if (!ReadSimpleResponse(r, out.error, out.questId, out.newStatus))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestRewardResponsePayload(uint8_t error, uint32_t questId, uint8_t newStatus)
	{
		std::vector<uint8_t> buf(6u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildQuestRewardResponsePacket(uint8_t error, uint32_t questId, uint8_t newStatus,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSimpleResponse(w, error, questId, newStatus))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeQuestRewardResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// QUEST_LIST
	// -------------------------------------------------------------------------

	std::optional<QuestListRequestPayload> ParseQuestListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte (0 octet ou plus, ignore le reste).
		return QuestListRequestPayload{};
	}

	std::vector<uint8_t> BuildQuestListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	namespace
	{
		/// Serialise la partie « apres error » de QUEST_LIST_RESPONSE.
		bool WriteQuestListBody(ByteWriter& w, uint8_t error, const std::vector<QuestStateEntry>& quests)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(quests.size())))
				return false;
			for (const auto& e : quests)
			{
				if (!w.WriteU32(e.questId))         return false;
				if (!w.WriteBytes(&e.status, 1u))   return false;
			}
			return true;
		}
	}

	std::optional<QuestListResponsePayload> ParseQuestListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.quests.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			QuestStateEntry e;
			if (!r.ReadU32(e.questId))         return std::nullopt;
			if (!r.ReadBytes(&e.status, 1u))   return std::nullopt;
			out.quests.push_back(e);
		}
		return out;
	}

	std::vector<uint8_t> BuildQuestListResponsePayload(uint8_t error, const std::vector<QuestStateEntry>& quests)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteQuestListBody(w, error, quests))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildQuestListResponsePacket(uint8_t error, const std::vector<QuestStateEntry>& quests,
	                                                  uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteQuestListBody(w, error, quests))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeQuestListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// QUEST_STATE_UPDATE (push, request_id=0)
	// -------------------------------------------------------------------------

	std::optional<QuestStateUpdatePayload> ParseQuestStateUpdatePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint32 questId (4) + uint8 newStatus (1) = 5.
		if (!payload || payloadSize < 5u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		QuestStateUpdatePayload out;
		if (!r.ReadU32(out.questId))         return std::nullopt;
		if (!r.ReadBytes(&out.newStatus, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildQuestStateUpdatePayload(uint32_t questId, uint8_t newStatus)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(questId))         return {};
		if (!w.WriteBytes(&newStatus, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildQuestStateUpdatePacket(uint32_t questId, uint8_t newStatus, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU32(questId))         return {};
		if (!w.WriteBytes(&newStatus, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0 (pas de request en correspondance cote client).
		if (!builder.Finalize(kOpcodeQuestStateUpdate, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
