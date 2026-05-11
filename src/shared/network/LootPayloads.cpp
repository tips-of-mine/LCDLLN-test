// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Implementation Parse/Build des
// payloads Loot.
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

#include "src/shared/network/LootPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	// -------------------------------------------------------------------------
	// LOOT_ROLL_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<LootRollNotificationPayload> ParseLootRollNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 rollId (8) + uint32 itemTemplateId (4) + uint16 strLen (2)
		//     + uint32 count (4) + uint32 durationSec (4) = 22 (string vide possible).
		if (!payload || payloadSize < 22u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LootRollNotificationPayload out;
		if (!r.ReadU64(out.rollId))         return std::nullopt;
		if (!r.ReadU32(out.itemTemplateId)) return std::nullopt;
		if (!r.ReadString(out.itemName))    return std::nullopt;
		if (!r.ReadU32(out.count))          return std::nullopt;
		if (!r.ReadU32(out.durationSec))    return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLootRollNotificationPayload(uint64_t rollId, uint32_t itemTemplateId, const std::string& itemName,
		uint32_t count, uint32_t durationSec)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(rollId))         return {};
		if (!w.WriteU32(itemTemplateId)) return {};
		if (!w.WriteString(itemName))    return {};
		if (!w.WriteU32(count))          return {};
		if (!w.WriteU32(durationSec))    return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLootRollNotificationPacket(uint64_t rollId, uint32_t itemTemplateId, const std::string& itemName,
		uint32_t count, uint32_t durationSec, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(rollId))         return {};
		if (!w.WriteU32(itemTemplateId)) return {};
		if (!w.WriteString(itemName))    return {};
		if (!w.WriteU32(count))          return {};
		if (!w.WriteU32(durationSec))    return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLootRollNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LOOT_ROLL_CHOICE - Request
	// -------------------------------------------------------------------------

	std::optional<LootRollChoiceRequestPayload> ParseLootRollChoiceRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 rollId (8) + uint8 choice (1) = 9.
		if (!payload || payloadSize < 9u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LootRollChoiceRequestPayload out;
		if (!r.ReadU64(out.rollId)) return std::nullopt;
		uint8_t choiceByte = 0;
		if (!r.ReadBytes(&choiceByte, 1u)) return std::nullopt;
		out.choice = choiceByte;
		return out;
	}

	std::vector<uint8_t> BuildLootRollChoiceRequestPayload(uint64_t rollId, uint8_t choice)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(rollId))         return {};
		if (!w.WriteBytes(&choice, 1u))  return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// LOOT_ROLL_CHOICE - Response
	// -------------------------------------------------------------------------

	std::optional<LootRollChoiceResponsePayload> ParseLootRollChoiceResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LootRollChoiceResponsePayload out;
		if (!r.ReadBytes(&out.status, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLootRollChoiceResponsePayload(uint8_t status)
	{
		std::vector<uint8_t> buf(1u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&status, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLootRollChoiceResponsePacket(uint8_t status, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&status, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLootRollChoiceResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LOOT_ROLL_RESULT_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<LootRollResultNotificationPayload> ParseLootRollResultNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 rollId (8) + uint16 winnerNameLen (2) + uint8 choice (1)
		//     + uint8 roll (1) + uint32 itemTemplateId (4) + uint16 itemNameLen (2)
		//     + uint32 count (4) = 22 (strings vides possibles).
		if (!payload || payloadSize < 22u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LootRollResultNotificationPayload out;
		if (!r.ReadU64(out.rollId))         return std::nullopt;
		if (!r.ReadString(out.winnerName))  return std::nullopt;
		uint8_t choiceByte = 0;
		if (!r.ReadBytes(&choiceByte, 1u))  return std::nullopt;
		out.winnerChoice = choiceByte;
		uint8_t rollByte = 0;
		if (!r.ReadBytes(&rollByte, 1u))    return std::nullopt;
		out.winnerRoll = rollByte;
		if (!r.ReadU32(out.itemTemplateId)) return std::nullopt;
		if (!r.ReadString(out.itemName))    return std::nullopt;
		if (!r.ReadU32(out.count))          return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLootRollResultNotificationPayload(uint64_t rollId, const std::string& winnerName,
		uint8_t winnerChoice, uint8_t winnerRoll, uint32_t itemTemplateId, const std::string& itemName, uint32_t count)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(rollId))             return {};
		if (!w.WriteString(winnerName))      return {};
		if (!w.WriteBytes(&winnerChoice, 1u)) return {};
		if (!w.WriteBytes(&winnerRoll, 1u))  return {};
		if (!w.WriteU32(itemTemplateId))     return {};
		if (!w.WriteString(itemName))        return {};
		if (!w.WriteU32(count))              return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLootRollResultNotificationPacket(uint64_t rollId, const std::string& winnerName,
		uint8_t winnerChoice, uint8_t winnerRoll, uint32_t itemTemplateId, const std::string& itemName, uint32_t count,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(rollId))             return {};
		if (!w.WriteString(winnerName))      return {};
		if (!w.WriteBytes(&winnerChoice, 1u)) return {};
		if (!w.WriteBytes(&winnerRoll, 1u))  return {};
		if (!w.WriteU32(itemTemplateId))     return {};
		if (!w.WriteString(itemName))        return {};
		if (!w.WriteU32(count))              return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLootRollResultNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// LOOT_SIMULATE_ROLL - Request
	// -------------------------------------------------------------------------

	std::optional<LootSimulateRollRequestPayload> ParseLootSimulateRollRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return LootSimulateRollRequestPayload{};
	}

	std::vector<uint8_t> BuildLootSimulateRollRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// LOOT_SIMULATE_ROLL - Response
	// -------------------------------------------------------------------------

	std::optional<LootSimulateRollResponsePayload> ParseLootSimulateRollResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 status (1) + uint64 rollId (8) = 9.
		if (!payload || payloadSize < 9u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		LootSimulateRollResponsePayload out;
		if (!r.ReadBytes(&out.status, 1u)) return std::nullopt;
		if (!r.ReadU64(out.rollId))        return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildLootSimulateRollResponsePayload(uint8_t status, uint64_t rollId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&status, 1u)) return {};
		if (!w.WriteU64(rollId))        return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildLootSimulateRollResponsePacket(uint8_t status, uint64_t rollId,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&status, 1u)) return {};
		if (!w.WriteU64(rollId))        return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeLootSimulateRollResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
