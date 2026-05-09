// CMANGOS.18 (Phase 3.18 step 3) — Implementation Parse/Build des payloads Mail.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilisé côté tests round-trip et
//     côté client pour les requests (envoyé via NetClient::SendRequest qui
//     ajoute le header).
//   - Build*ResponsePacket utilise PacketBuilder pour assembler le paquet
//     complet header + payload, prêt à passer à NetServer::Send. Le
//     requestId vient du paquet d'origine (cf. RequestResponseDispatcher).
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/MailPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::network
{
	namespace
	{
		/// Helper : construit un payload dans un buffer scratch dimensionné à la
		/// limite protocole, puis le tronque à l'offset réel. Évite de penser à la
		/// taille exacte d'avance pour les payloads contenant des strings/vector.
		std::vector<uint8_t> MakeScratchBuffer()
		{
			return std::vector<uint8_t>(kProtocolV1MaxPacketSize, 0u);
		}
	}

	// -------------------------------------------------------------------------
	// MAIL_SEND
	// -------------------------------------------------------------------------

	std::optional<MailSendRequestPayload> ParseMailSendRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint64 recipient (8) + uint16 subject_len (2) + uint16 body_len (2)
		//       + uint64 gold (8) + uint64 cod (8) = 28 octets si subject/body vides.
		if (!payload || payloadSize < 28u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailSendRequestPayload out;
		if (!r.ReadU64(out.recipientAccountId))
			return std::nullopt;
		if (!r.ReadString(out.subject))
			return std::nullopt;
		if (!r.ReadString(out.body))
			return std::nullopt;
		if (!r.ReadU64(out.copperGold))
			return std::nullopt;
		if (!r.ReadU64(out.copperCod))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailSendRequestPayload(uint64_t recipientAccountId,
	                                                 std::string_view subject,
	                                                 std::string_view body,
	                                                 uint64_t copperGold,
	                                                 uint64_t copperCod)
	{
		// Tronque défensivement avant l'appel ByteWriter pour éviter qu'une string
		// > kProtocolV1MaxStringLength fasse échouer WriteString.
		if (subject.size() > kMaxMailSubjectBytes)
			subject = subject.substr(0, kMaxMailSubjectBytes);
		if (body.size() > kMaxMailBodyBytes)
			body = body.substr(0, kMaxMailBodyBytes);

		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(recipientAccountId))      return {};
		if (!w.WriteString(subject))              return {};
		if (!w.WriteString(body))                 return {};
		if (!w.WriteU64(copperGold))              return {};
		if (!w.WriteU64(copperCod))               return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MailSendResponsePayload> ParseMailSendResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 mailId (8) = 9 octets.
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailSendResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (!r.ReadU64(out.mailId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailSendResponsePayload(uint8_t error, uint64_t mailId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildMailSendResponsePacket(uint8_t error, uint64_t mailId,
	                                                 uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMailSendResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// MAIL_LIST_INBOX
	// -------------------------------------------------------------------------

	std::optional<MailListInboxRequestPayload> ParseMailListInboxRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepté (0 octet ou plus, ignore le reste).
		return MailListInboxRequestPayload{};
	}

	std::vector<uint8_t> BuildMailListInboxRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	std::optional<MailListInboxResponsePayload> ParseMailListInboxResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1). Si error=0, on continue avec uint16 count + entries.
		if (!payload || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailListInboxResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count))
			return std::nullopt;
		out.mails.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			MailInboxEntry e;
			if (!r.ReadU64(e.mailId))           return std::nullopt;
			if (!r.ReadU64(e.senderAccountId))  return std::nullopt;
			if (!r.ReadString(e.subject))       return std::nullopt;
			if (!r.ReadU64(e.sentTsMs))         return std::nullopt;
			if (!r.ReadU64(e.expiresTsMs))      return std::nullopt;
			if (!r.ReadBytes(&e.state, 1u))     return std::nullopt;
			if (!r.ReadU64(e.copperGold))       return std::nullopt;
			if (!r.ReadU64(e.copperCod))        return std::nullopt;
			out.mails.push_back(std::move(e));
		}
		return out;
	}

	namespace
	{
		/// Sérialise la partie « après error » de MAIL_LIST_INBOX_RESPONSE.
		/// Mutualisé entre payload-only et packet builder.
		bool WriteListInboxBody(ByteWriter& w, uint8_t error, const std::vector<MailInboxEntry>& mails)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			if (!w.WriteArrayCount(static_cast<uint16_t>(mails.size())))
				return false;
			for (const auto& e : mails)
			{
				if (!w.WriteU64(e.mailId))           return false;
				if (!w.WriteU64(e.senderAccountId))  return false;
				if (!w.WriteString(e.subject))       return false;
				if (!w.WriteU64(e.sentTsMs))         return false;
				if (!w.WriteU64(e.expiresTsMs))      return false;
				if (!w.WriteBytes(&e.state, 1u))     return false;
				if (!w.WriteU64(e.copperGold))       return false;
				if (!w.WriteU64(e.copperCod))        return false;
			}
			return true;
		}
	}

	std::vector<uint8_t> BuildMailListInboxResponsePayload(uint8_t error, const std::vector<MailInboxEntry>& mails)
	{
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!WriteListInboxBody(w, error, mails))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildMailListInboxResponsePacket(uint8_t error, const std::vector<MailInboxEntry>& mails,
	                                                      uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteListInboxBody(w, error, mails))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMailListInboxResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// MAIL_READ
	// -------------------------------------------------------------------------

	std::optional<MailReadRequestPayload> ParseMailReadRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailReadRequestPayload out;
		if (!r.ReadU64(out.mailId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailReadRequestPayload(uint64_t mailId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(mailId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MailReadResponsePayload> ParseMailReadResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint8 error (1) + uint64 mailId (8) + uint16 body_len (2) = 11.
		if (!payload || payloadSize < 11u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailReadResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))    return std::nullopt;
		if (!r.ReadU64(out.mailId))          return std::nullopt;
		if (!r.ReadString(out.body))         return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailReadResponsePayload(uint8_t error, uint64_t mailId, std::string_view body)
	{
		if (body.size() > kMaxMailBodyBytes)
			body = body.substr(0, kMaxMailBodyBytes);
		auto buf = MakeScratchBuffer();
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		if (!w.WriteString(body))        return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildMailReadResponsePacket(uint8_t error, uint64_t mailId, std::string_view body,
	                                                 uint32_t requestId, uint64_t sessionIdHeader)
	{
		if (body.size() > kMaxMailBodyBytes)
			body = body.substr(0, kMaxMailBodyBytes);
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		if (!w.WriteString(body))        return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMailReadResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// MAIL_TAKE_ATTACHMENTS
	// -------------------------------------------------------------------------

	std::optional<MailTakeAttachmentsRequestPayload> ParseMailTakeAttachmentsRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 16u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailTakeAttachmentsRequestPayload out;
		if (!r.ReadU64(out.mailId))         return std::nullopt;
		if (!r.ReadU64(out.paidCopperCod))  return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailTakeAttachmentsRequestPayload(uint64_t mailId, uint64_t paidCopperCod)
	{
		std::vector<uint8_t> buf(16u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(mailId))         return {};
		if (!w.WriteU64(paidCopperCod))  return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MailTakeAttachmentsResponsePayload> ParseMailTakeAttachmentsResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 mailId (8) + uint64 copperGoldTaken (8) = 17.
		if (!payload || payloadSize < 17u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailTakeAttachmentsResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))     return std::nullopt;
		if (!r.ReadU64(out.mailId))           return std::nullopt;
		if (!r.ReadU64(out.copperGoldTaken))  return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailTakeAttachmentsResponsePayload(uint8_t error, uint64_t mailId, uint64_t copperGoldTaken)
	{
		std::vector<uint8_t> buf(17u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))     return {};
		if (!w.WriteU64(mailId))           return {};
		if (!w.WriteU64(copperGoldTaken))  return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildMailTakeAttachmentsResponsePacket(uint8_t error, uint64_t mailId, uint64_t copperGoldTaken,
	                                                             uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))     return {};
		if (!w.WriteU64(mailId))           return {};
		if (!w.WriteU64(copperGoldTaken))  return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMailTakeAttachmentsResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// MAIL_DELETE
	// -------------------------------------------------------------------------

	std::optional<MailDeleteRequestPayload> ParseMailDeleteRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 8u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailDeleteRequestPayload out;
		if (!r.ReadU64(out.mailId))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailDeleteRequestPayload(uint64_t mailId)
	{
		std::vector<uint8_t> buf(8u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU64(mailId))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MailDeleteResponsePayload> ParseMailDeleteResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		// uint8 error (1) + uint64 mailId (8) = 9.
		if (!payload || payloadSize < 9u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MailDeleteResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))   return std::nullopt;
		if (!r.ReadU64(out.mailId))         return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMailDeleteResponsePayload(uint8_t error, uint64_t mailId)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildMailDeleteResponsePacket(uint8_t error, uint64_t mailId,
	                                                    uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u))   return {};
		if (!w.WriteU64(mailId))         return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMailDeleteResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
