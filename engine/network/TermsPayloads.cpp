#include "engine/network/TermsPayloads.h"
#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"

namespace engine::network
{
	std::optional<TermsStatusRequestPayload> ParseTermsStatusRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TermsStatusRequestPayload out;
		if (!r.ReadString(out.locale_pref))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTermsStatusResponsePacket(const TermsStatusResponsePayload& p, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder pb;
		ByteWriter w = pb.PayloadWriter();
		if (!w.WriteU32(p.pending_count) || !w.WriteU64(p.next_edition_id) || !w.WriteString(p.version_label)
		    || !w.WriteString(p.title) || !w.WriteString(p.resolved_locale))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!pb.Finalize(kOpcodeTermsStatusResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return pb.Data();
	}

	std::optional<TermsContentRequestPayload> ParseTermsContentRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TermsContentRequestPayload out;
		if (!r.ReadU64(out.edition_id) || !r.ReadString(out.locale_pref) || !r.ReadU32(out.byte_offset) || !r.ReadU32(out.max_chunk))
			return std::nullopt;
		if (out.max_chunk == 0 || out.max_chunk > 8192u)
			out.max_chunk = 4096;
		return out;
	}

	std::vector<uint8_t> BuildTermsContentResponsePacket(const TermsContentResponsePayload& p, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder pb;
		ByteWriter w = pb.PayloadWriter();
		if (!w.WriteU64(p.edition_id) || !w.WriteU32(p.byte_offset) || !w.WriteU32(p.total_length) || !w.WriteString(p.chunk))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!pb.Finalize(kOpcodeTermsContentResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return pb.Data();
	}

	std::optional<TermsAcceptRequestPayload> ParseTermsAcceptRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		TermsAcceptRequestPayload out;
		if (!r.ReadU64(out.edition_id))
			return std::nullopt;
		if (!r.ReadBytes(reinterpret_cast<uint8_t*>(&out.acknowledged), 1u))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildTermsAcceptResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder pb;
		ByteWriter w = pb.PayloadWriter();
		if (!w.WriteBytes(&success, 1))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!pb.Finalize(kOpcodeTermsAcceptResponse, 0, requestId, sessionIdHeader, payloadBytes))
			return {};
		return pb.Data();
	}
}
