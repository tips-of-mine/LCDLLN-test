// CMANGOS.32 (Phase 5.32 step 3+4) — Implementation Parse/Build des payloads GmTickets.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     cote client pour les requests (envoye via SendGenericRequestAsync qui
//     ajoute le header).
//   - Build*ResponsePacket utilise PacketBuilder pour assembler le paquet
//     complet header + payload, pret a passer a NetServer::Send. Le requestId
//     vient du paquet d'origine (cf. RequestResponseDispatcher).
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/GmTicketPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Helper : construit un payload dans un buffer scratch dimensionne a la
		/// limite protocole, puis le tronque a l'offset reel. Evite de penser a
		/// la taille exacte d'avance pour les payloads contenant des strings/vector.
		std::vector<uint8_t> MakeScratchBuffer()
		{
			return std::vector<uint8_t>(kProtocolV1MaxPacketSize, 0u);
		}

		/// Ecrit (uint8 error, uint64 ticketId). Mutualise pour les responses
		/// Open (77) et Cancel (81) qui partagent le meme format.
		bool WriteSimpleErrorAndTicket(ByteWriter& w, uint8_t error, uint64_t ticketId)
		{
			if (!w.WriteBytes(&error, 1u)) return false;
			if (!w.WriteU64(ticketId))     return false;
			return true;
		}

		/// Lit (uint8 error, uint64 ticketId). Symetrique de WriteSimpleErrorAndTicket.
		bool ReadSimpleErrorAndTicket(ByteReader& r, uint8_t& error, uint64_t& ticketId)
		{
			if (!r.ReadBytes(&error, 1u)) return false;
			if (!r.ReadU64(ticketId))     return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// GMTICKET_OPEN
	// -------------------------------------------------------------------------

	std::optional<GmTicketOpenRequestPayload> ParseGmTicketOpenRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint16 body_len (2). Body peut etre vide cote wire (le serveur
		// retournera BodyEmpty), c'est mieux qu'un parse failure pour avoir un
		// retour codifie.
		if (!payload || payloadSize < 2u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketOpenRequestPayload out;
		if (!r.ReadString(out.body))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGmTicketOpenRequestPayload(std::string_view body)
	{
		// Tronque defensivement avant l'appel ByteWriter pour eviter qu'une string
		// > kProtocolV1MaxStringLength fasse echouer WriteString.
		if (body.size() > kMaxGmTicketBodyBytes)
			body = body.substr(0, kMaxGmTicketBodyBytes);

		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteString(body))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<GmTicketOpenResponsePayload> ParseGmTicketOpenResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 ticketId (8) = 9 octets.
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketOpenResponsePayload out;
		if (!ReadSimpleErrorAndTicket(r, out.error, out.ticketId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGmTicketOpenResponsePayload(uint8_t error, uint64_t ticketId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSimpleErrorAndTicket(w, error, ticketId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGmTicketOpenResponsePacket(uint8_t error, uint64_t ticketId,
	                                                     uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSimpleErrorAndTicket(w, error, ticketId))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGmTicketOpenResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GMTICKET_LIST_MINE
	// -------------------------------------------------------------------------

	std::optional<GmTicketListMineRequestPayload> ParseGmTicketListMineRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte (0 octet ou plus, ignore le reste).
		return GmTicketListMineRequestPayload{};
	}

	std::vector<uint8_t> BuildGmTicketListMineRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	namespace
	{
		/// Serialise la partie « apres error » de GMTICKET_LIST_MINE_RESPONSE.
		bool WriteListMineBody(ByteWriter& w, uint8_t error, const std::vector<GmTicketEntry>& tickets)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(tickets.size())))
				return false;
			for (const auto& t : tickets)
			{
				if (!w.WriteU64(t.id))             return false;
				if (!w.WriteU64(t.createdTsMs))    return false;
				if (!w.WriteU64(t.resolvedTsMs))   return false;
				if (!w.WriteBytes(&t.state, 1u))   return false;
			}
			return true;
		}
	}

	std::optional<GmTicketListMineResponsePayload> ParseGmTicketListMineResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1).
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketListMineResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.tickets.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GmTicketEntry t;
			if (!r.ReadU64(t.id))             return std::nullopt;
			if (!r.ReadU64(t.createdTsMs))    return std::nullopt;
			if (!r.ReadU64(t.resolvedTsMs))   return std::nullopt;
			if (!r.ReadBytes(&t.state, 1u))   return std::nullopt;
			out.tickets.push_back(t);
		}
		return out;
	}

	std::vector<uint8_t> BuildGmTicketListMineResponsePayload(uint8_t error, const std::vector<GmTicketEntry>& tickets)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!WriteListMineBody(w, error, tickets))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGmTicketListMineResponsePacket(uint8_t error, const std::vector<GmTicketEntry>& tickets,
	                                                         uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteListMineBody(w, error, tickets))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGmTicketListMineResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GMTICKET_CANCEL
	// -------------------------------------------------------------------------

	std::optional<GmTicketCancelRequestPayload> ParseGmTicketCancelRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketCancelRequestPayload out;
		if (!r.ReadU64(out.ticketId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGmTicketCancelRequestPayload(uint64_t ticketId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(ticketId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<GmTicketCancelResponsePayload> ParseGmTicketCancelResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketCancelResponsePayload out;
		if (!ReadSimpleErrorAndTicket(r, out.error, out.ticketId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGmTicketCancelResponsePayload(uint8_t error, uint64_t ticketId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteSimpleErrorAndTicket(w, error, ticketId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGmTicketCancelResponsePacket(uint8_t error, uint64_t ticketId,
	                                                       uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteSimpleErrorAndTicket(w, error, ticketId))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGmTicketCancelResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GMTICKET_RESOLVED_NOTIFICATION (push, request_id=0)
	// -------------------------------------------------------------------------

	std::optional<GmTicketResolvedNotificationPayload> ParseGmTicketResolvedNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint64 ticketId (8) + uint64 resolvedTsMs (8) = 16 octets.
		if (!payload || payloadSize < 16u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		GmTicketResolvedNotificationPayload out;
		if (!r.ReadU64(out.ticketId))     return std::nullopt;
		if (!r.ReadU64(out.resolvedTsMs)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGmTicketResolvedNotificationPayload(uint64_t ticketId, uint64_t resolvedTsMs)
	{
		std::vector<uint8_t> buf(16u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(ticketId))     return {};
		if (!w.WriteU64(resolvedTsMs)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGmTicketResolvedNotificationPacket(uint64_t ticketId, uint64_t resolvedTsMs,
	                                                              uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU64(ticketId))     return {};
		if (!w.WriteU64(resolvedTsMs)) return {};
		const size_t payloadBytes = w.Offset();
		// Push : requestId=0 (pas de request en correspondance cote client).
		if (!builder.Finalize(kOpcodeGmTicketResolvedNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
