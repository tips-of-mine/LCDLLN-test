#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// Client → Master: preferred locale for CGU text (e.g. "fr", empty = server picks from account default if known).
	struct TermsStatusRequestPayload
	{
		std::string locale_pref;
	};

	/// Master → Client: pending CGU count and next edition to show (if any).
	struct TermsStatusResponsePayload
	{
		uint32_t    pending_count   = 0;
		uint64_t    next_edition_id = 0;
		std::string version_label;
		std::string title;
		std::string resolved_locale;
	};

	/// Client → Master: fetch body chunk (large CGU).
	struct TermsContentRequestPayload
	{
		uint64_t    edition_id = 0;
		std::string locale_pref;
		uint32_t    byte_offset = 0;
		uint32_t    max_chunk   = 4096;
	};

	struct TermsContentResponsePayload
	{
		uint64_t    edition_id = 0;
		uint32_t    byte_offset = 0;
		uint32_t    total_length = 0;
		std::string chunk;
	};

	/// Client → Master: record acceptance of one edition (session required).
	struct TermsAcceptRequestPayload
	{
		uint64_t edition_id    = 0;
		uint8_t  acknowledged  = 0; ///< must be 1
	};

	std::optional<TermsStatusRequestPayload> ParseTermsStatusRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildTermsStatusResponsePacket(const TermsStatusResponsePayload& p, uint32_t requestId, uint64_t sessionIdHeader);

	std::optional<TermsContentRequestPayload> ParseTermsContentRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildTermsContentResponsePacket(const TermsContentResponsePayload& p, uint32_t requestId, uint64_t sessionIdHeader);

	std::optional<TermsAcceptRequestPayload> ParseTermsAcceptRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildTermsAcceptResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);
}
