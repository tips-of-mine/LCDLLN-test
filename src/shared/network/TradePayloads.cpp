// CMANGOS.27 (Phase 4.27 step 3+4) -- Implementation Parse/Build des payloads Trade.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     cote client pour les requests (envoye via SendGenericRequestAsync qui
//     ajoute le header).
//   - Build*ResponsePacket / Build*NotificationPacket utilise PacketBuilder
//     pour assembler le paquet complet header + payload, pret a passer a
//     NetServer::Send. Le requestId vient du paquet d'origine pour les
//     responses ; les notifications utilisent requestId=0 (push asynchrone).
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/TradePayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Helper : construit un payload dans un buffer scratch dimensionne a la
		/// limite protocole, puis le tronque a l'offset reel. Evite de calculer
		/// la taille exacte d'avance pour les payloads de longueur variable
		/// (offer items vector, cancel reason string, ...).
		std::vector<uint8_t> MakeScratchBuffer()
		{
			return std::vector<uint8_t>(kProtocolV1MaxPacketSize, 0u);
		}

		/// Ecrit (uint64 sessionId, uint64 copperGold, uint16 itemCount + uint64 GUIDs).
		/// Mutualise pour SetOfferRequest (86) et StateUpdateNotification (90),
		/// qui partagent la meme sequence de champs (offer payload variable).
		bool WriteOfferBody(ByteWriter& w, uint64_t copperGold, const std::vector<uint64_t>& itemGuids)
		{
			if (!w.WriteU64(copperGold))
				return false;
			// Tronque defensivement a kMaxTradeItemsPerOffer pour eviter un
			// payload abusif. Le serveur fera aussi un check par securite.
			const size_t count = (itemGuids.size() > kMaxTradeItemsPerOffer)
				? kMaxTradeItemsPerOffer : itemGuids.size();
			if (!w.WriteArrayCount(static_cast<uint16_t>(count)))
				return false;
			for (size_t i = 0; i < count; ++i)
			{
				if (!w.WriteU64(itemGuids[i]))
					return false;
			}
			return true;
		}

		/// Lit (uint64 copperGold, uint16 itemCount + uint64 GUIDs). Symetrique
		/// de WriteOfferBody.
		bool ReadOfferBody(ByteReader& r, uint64_t& copperGold, std::vector<uint64_t>& itemGuids)
		{
			if (!r.ReadU64(copperGold))
				return false;
			uint16_t count = 0;
			if (!r.ReadArrayCount(count))
				return false;
			if (count > kMaxTradeItemsPerOffer)
				return false; // payload abusif rejete
			itemGuids.clear();
			itemGuids.reserve(count);
			for (uint16_t i = 0; i < count; ++i)
			{
				uint64_t guid = 0;
				if (!r.ReadU64(guid))
					return false;
				itemGuids.push_back(guid);
			}
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// TRADE_BEGIN
	// -------------------------------------------------------------------------

	std::optional<TradeBeginRequestPayload> ParseTradeBeginRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeBeginRequestPayload out;
		if (!r.ReadU64(out.targetAccountId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeBeginRequestPayload(uint64_t targetAccountId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(targetAccountId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<TradeBeginResponsePayload> ParseTradeBeginResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 sessionId (8) + uint64 partnerAccountId (8) = 17 octets.
		if (!payload || payloadSize < 17u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeBeginResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))   return std::nullopt;
		if (!r.ReadU64(out.sessionId))      return std::nullopt;
		if (!r.ReadU64(out.partnerAccountId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeBeginResponsePayload(uint8_t error, uint64_t sessionId, uint64_t partnerAccountId)
	{
		std::vector<uint8_t> buf(17u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(sessionId))      return {};
		if (!w.WriteU64(partnerAccountId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeBeginResponsePacket(uint8_t error, uint64_t sessionId, uint64_t partnerAccountId,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(sessionId))      return {};
		if (!w.WriteU64(partnerAccountId)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeTradeBeginResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::optional<TradeBeginNotificationPayload> ParseTradeBeginNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint64 sessionId + uint64 partnerAccountId = 16 octets.
		if (!payload || payloadSize < 16u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeBeginNotificationPayload out;
		if (!r.ReadU64(out.sessionId))        return std::nullopt;
		if (!r.ReadU64(out.partnerAccountId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeBeginNotificationPayload(uint64_t sessionId, uint64_t partnerAccountId)
	{
		std::vector<uint8_t> buf(16u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))        return {};
		if (!w.WriteU64(partnerAccountId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeBeginNotificationPacket(uint64_t sessionId, uint64_t partnerAccountId,
	                                                        uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(sessionId))        return {};
		if (!w.WriteU64(partnerAccountId)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0 (pas de request en correspondance cote target).
		if (!builder.Finalize(kOpcodeTradeBeginNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// TRADE_SET_OFFER
	// -------------------------------------------------------------------------

	std::optional<TradeSetOfferRequestPayload> ParseTradeSetOfferRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 sessionId (8) + uint64 copperGold (8) + uint16 count (2) = 18 octets.
		if (!payload || payloadSize < 18u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeSetOfferRequestPayload out;
		if (!r.ReadU64(out.sessionId)) return std::nullopt;
		if (!ReadOfferBody(r, out.copperGold, out.itemGuids))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeSetOfferRequestPayload(uint64_t sessionId, uint64_t copperGold,
	                                                       const std::vector<uint64_t>& itemGuids)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))     return {};
		if (!WriteOfferBody(w, copperGold, itemGuids)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<TradeSetOfferResponsePayload> ParseTradeSetOfferResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeSetOfferResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeSetOfferResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeSetOfferResponsePacket(uint8_t error,
	                                                       uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeTradeSetOfferResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// TRADE_LOCK
	// -------------------------------------------------------------------------

	std::optional<TradeLockRequestPayload> ParseTradeLockRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeLockRequestPayload out;
		if (!r.ReadU64(out.sessionId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeLockRequestPayload(uint64_t sessionId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<TradeLockResponsePayload> ParseTradeLockResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error + uint8 newState = 2 octets.
		if (!payload || payloadSize < 2u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeLockResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))    return std::nullopt;
		if (!r.ReadBytes(&out.newState, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeLockResponsePayload(uint8_t error, uint8_t newState)
	{
		std::vector<uint8_t> buf(2u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))    return {};
		if (!w.WriteBytes(&newState, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeLockResponsePacket(uint8_t error, uint8_t newState,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))    return {};
		if (!w.WriteBytes(&newState, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeTradeLockResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// TRADE_STATE_UPDATE_NOTIFICATION (push)
	// -------------------------------------------------------------------------

	std::optional<TradeStateUpdateNotificationPayload> ParseTradeStateUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 sessionId (8) + uint8 state (1) + uint64 copperGold (8) + uint16 count (2) = 19 octets.
		if (!payload || payloadSize < 19u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeStateUpdateNotificationPayload out;
		if (!r.ReadU64(out.sessionId))     return std::nullopt;
		if (!r.ReadBytes(&out.state, 1u))  return std::nullopt;
		if (!ReadOfferBody(r, out.partnerCopperGold, out.partnerItemGuids))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeStateUpdateNotificationPayload(uint64_t sessionId, uint8_t state,
	                                                                uint64_t partnerCopperGold,
	                                                                const std::vector<uint64_t>& partnerItemGuids)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))     return {};
		if (!w.WriteBytes(&state, 1u))  return {};
		if (!WriteOfferBody(w, partnerCopperGold, partnerItemGuids))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeStateUpdateNotificationPacket(uint64_t sessionId, uint8_t state,
	                                                               uint64_t partnerCopperGold,
	                                                               const std::vector<uint64_t>& partnerItemGuids,
	                                                               uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(sessionId))     return {};
		if (!w.WriteBytes(&state, 1u))  return {};
		if (!WriteOfferBody(w, partnerCopperGold, partnerItemGuids))
			return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeTradeStateUpdateNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// TRADE_COMMIT
	// -------------------------------------------------------------------------

	std::optional<TradeCommitRequestPayload> ParseTradeCommitRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeCommitRequestPayload out;
		if (!r.ReadU64(out.sessionId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeCommitRequestPayload(uint64_t sessionId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<TradeCommitResponsePayload> ParseTradeCommitResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeCommitResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeCommitResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeCommitResponsePacket(uint8_t error,
	                                                     uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeTradeCommitResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// TRADE_CANCEL
	// -------------------------------------------------------------------------

	std::optional<TradeCancelRequestPayload> ParseTradeCancelRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeCancelRequestPayload out;
		if (!r.ReadU64(out.sessionId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeCancelRequestPayload(uint64_t sessionId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<TradeCancelNotificationPayload> ParseTradeCancelNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 sessionId (8) + uint16 reason_len (2) = 10 octets.
		if (!payload || payloadSize < 10u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TradeCancelNotificationPayload out;
		if (!r.ReadU64(out.sessionId)) return std::nullopt;
		if (!r.ReadString(out.reason)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTradeCancelNotificationPayload(uint64_t sessionId, std::string_view reason)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(sessionId)) return {};
		if (!w.WriteString(reason)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildTradeCancelNotificationPacket(uint64_t sessionId, std::string_view reason,
	                                                          uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(sessionId)) return {};
		if (!w.WriteString(reason)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0.
		if (!builder.Finalize(kOpcodeTradeCancelNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
