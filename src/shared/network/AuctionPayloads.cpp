// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Implementation Parse/Build
// des payloads Auction.
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

#include "src/shared/network/AuctionPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	namespace
	{
		/// Ecrit un AuctionListingSummary complet sur le wire.
		bool WriteAuctionListingSummary(ByteWriter& w, const AuctionListingSummary& s)
		{
			if (!w.WriteU64(s.auctionId))              return false;
			if (!w.WriteU32(s.itemTemplateId))         return false;
			if (!w.WriteString(s.itemName))            return false;
			if (!w.WriteU32(s.count))                  return false;
			if (!w.WriteU64(s.currentBidCopper))       return false;
			if (!w.WriteU64(s.buyoutCopper))           return false;
			if (!w.WriteString(s.ownerName))           return false;
			if (!w.WriteU64(s.secondsUntilExpiration)) return false;
			return true;
		}

		/// Lit un AuctionListingSummary depuis le wire.
		bool ReadAuctionListingSummary(ByteReader& r, AuctionListingSummary& out)
		{
			if (!r.ReadU64(out.auctionId))              return false;
			if (!r.ReadU32(out.itemTemplateId))         return false;
			if (!r.ReadString(out.itemName))            return false;
			if (!r.ReadU32(out.count))                  return false;
			if (!r.ReadU64(out.currentBidCopper))       return false;
			if (!r.ReadU64(out.buyoutCopper))           return false;
			if (!r.ReadString(out.ownerName))           return false;
			if (!r.ReadU64(out.secondsUntilExpiration)) return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// AUCTION_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<AuctionListRequestPayload> ParseAuctionListRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionListRequestPayload out;
		if (!r.ReadU32(out.itemTemplateIdFilter)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionListRequestPayload(uint32_t itemTemplateIdFilter)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(itemTemplateIdFilter)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// AUCTION_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<AuctionListResponsePayload> ParseAuctionListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.listings.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			AuctionListingSummary s;
			if (!ReadAuctionListingSummary(r, s)) return std::nullopt;
			out.listings.push_back(std::move(s));
		}
		return out;
	}

	std::vector<uint8_t> BuildAuctionListResponsePayload(uint8_t error, const std::vector<AuctionListingSummary>& listings)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(listings.size()))) return {};
			for (const auto& s : listings)
			{
				if (!WriteAuctionListingSummary(w, s)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildAuctionListResponsePacket(uint8_t error, const std::vector<AuctionListingSummary>& listings,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(listings.size()))) return {};
			for (const auto& s : listings)
			{
				if (!WriteAuctionListingSummary(w, s)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuctionListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// AUCTION_POST — Request
	// -------------------------------------------------------------------------

	std::optional<AuctionPostRequestPayload> ParseAuctionPostRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 itemTemplateId (4) + uint32 count (4) + uint64 startBid (8)
		//     + uint64 buyout (8) + uint8 durationHours (1) = 25.
		if (!payload || payloadSize < 25u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionPostRequestPayload out;
		if (!r.ReadU32(out.itemTemplateId))   return std::nullopt;
		if (!r.ReadU32(out.count))            return std::nullopt;
		if (!r.ReadU64(out.startBidCopper))   return std::nullopt;
		if (!r.ReadU64(out.buyoutCopper))     return std::nullopt;
		if (!r.ReadBytes(&out.durationHours, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionPostRequestPayload(uint32_t itemTemplateId, uint32_t count,
		uint64_t startBidCopper, uint64_t buyoutCopper, uint8_t durationHours)
	{
		std::vector<uint8_t> buf(25u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(itemTemplateId))   return {};
		if (!w.WriteU32(count))            return {};
		if (!w.WriteU64(startBidCopper))   return {};
		if (!w.WriteU64(buyoutCopper))     return {};
		if (!w.WriteBytes(&durationHours, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// AUCTION_POST — Response
	// -------------------------------------------------------------------------

	std::optional<AuctionPostResponsePayload> ParseAuctionPostResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionPostResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		if (!r.ReadU64(out.auctionId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionPostResponsePayload(uint8_t error, uint64_t auctionId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU64(auctionId)) return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildAuctionPostResponsePacket(uint8_t error, uint64_t auctionId,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteU64(auctionId)) return {};
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuctionPostResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// AUCTION_BID — Request
	// -------------------------------------------------------------------------

	std::optional<AuctionBidRequestPayload> ParseAuctionBidRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 auctionId (8) + uint64 bidAmount (8) = 16.
		if (!payload || payloadSize < 16u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionBidRequestPayload out;
		if (!r.ReadU64(out.auctionId))       return std::nullopt;
		if (!r.ReadU64(out.bidAmountCopper)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionBidRequestPayload(uint64_t auctionId, uint64_t bidAmountCopper)
	{
		std::vector<uint8_t> buf(16u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(auctionId))       return {};
		if (!w.WriteU64(bidAmountCopper)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// AUCTION_BID — Response
	// -------------------------------------------------------------------------

	std::optional<AuctionBidResponsePayload> ParseAuctionBidResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionBidResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		if (!r.ReadBytes(&out.isBuyout, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionBidResponsePayload(uint8_t error, uint8_t isBuyout)
	{
		std::vector<uint8_t> buf(2u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteBytes(&isBuyout, 1u)) return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildAuctionBidResponsePacket(uint8_t error, uint8_t isBuyout,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteBytes(&isBuyout, 1u)) return {};
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuctionBidResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// AUCTION_CANCEL — Request
	// -------------------------------------------------------------------------

	std::optional<AuctionCancelRequestPayload> ParseAuctionCancelRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionCancelRequestPayload out;
		if (!r.ReadU64(out.auctionId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionCancelRequestPayload(uint64_t auctionId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(auctionId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// AUCTION_CANCEL — Response
	// -------------------------------------------------------------------------

	std::optional<AuctionCancelResponsePayload> ParseAuctionCancelResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionCancelResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionCancelResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildAuctionCancelResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuctionCancelResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// AUCTION_EXPIRED_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<AuctionExpiredNotificationPayload> ParseAuctionExpiredNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 auctionId (8) + uint8 won (1) + uint64 finalBid (8)
		//     + uint16 string length (2) = 19 (winnerName vide possible).
		if (!payload || payloadSize < 19u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		AuctionExpiredNotificationPayload out;
		if (!r.ReadU64(out.auctionId))     return std::nullopt;
		if (!r.ReadBytes(&out.won, 1u))    return std::nullopt;
		if (!r.ReadU64(out.finalBidCopper)) return std::nullopt;
		if (!r.ReadString(out.winnerName)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildAuctionExpiredNotificationPayload(uint64_t auctionId, uint8_t won,
		uint64_t finalBidCopper, const std::string& winnerName)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(auctionId))     return {};
		if (!w.WriteBytes(&won, 1u))    return {};
		if (!w.WriteU64(finalBidCopper)) return {};
		if (!w.WriteString(winnerName)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildAuctionExpiredNotificationPacket(uint64_t auctionId, uint8_t won,
		uint64_t finalBidCopper, const std::string& winnerName, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(auctionId))     return {};
		if (!w.WriteBytes(&won, 1u))    return {};
		if (!w.WriteU64(finalBidCopper)) return {};
		if (!w.WriteString(winnerName)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeAuctionExpiredNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
