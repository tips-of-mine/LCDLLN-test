#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// 128-bit ticket id (M22.4).
	constexpr size_t kShardTicketIdSize = 16u;
	using ShardTicketId = std::array<uint8_t, kShardTicketIdSize>;

	/// HMAC-SHA256 size for ticket signature (v1).
	constexpr size_t kShardTicketHmacSize = 32u;

	/// Parsed ticket data (ticket_id, account_id, target_shard_id, expires_at, character_id).
	/// HMAC verified separately. \c character_id (TA.3) lie la session UDP au personnage :
	/// il est couvert par le HMAC (non falsifiable). 0 = aucun personnage (le client n'a pas
	/// encore fait EnterWorld au moment de la demande de ticket).
	struct ShardTicketData
	{
		ShardTicketId ticket_id{};
		uint64_t account_id = 0;
		uint32_t target_shard_id = 0;
		uint64_t expires_at = 0; ///< Seconds since epoch (system_clock).
		uint64_t character_id = 0; ///< TA.3 : personnage lié, authentifié par le HMAC.
		std::array<uint8_t, kShardTicketHmacSize> hmac{};
	};

	/// Request payload: client asks Master for a ticket to a shard.
	struct RequestShardTicketPayload
	{
		uint32_t target_shard_id = 0;
	};

	/// Parses REQUEST_SHARD_TICKET payload. Returns nullopt if invalid.
	std::optional<RequestShardTicketPayload> ParseRequestShardTicketPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds REQUEST_SHARD_TICKET payload (Client→Master).
	std::vector<uint8_t> BuildRequestShardTicketPayload(uint32_t target_shard_id);

	/// Parses ticket blob (from SHARD_TICKET_RESPONSE or PRESENT_SHARD_TICKET). Returns nullopt if truncated/invalid.
	std::optional<ShardTicketData> ParseShardTicketPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds ticket blob (ticket_id, account_id, target_shard_id, expires_at, character_id, hmac).
	/// Master calls this after computing HMAC (le HMAC doit couvrir character_id, cf. ComputeTicketHmac).
	std::vector<uint8_t> BuildShardTicketPayload(const ShardTicketId& ticket_id, uint64_t account_id,
		uint32_t target_shard_id, uint64_t expires_at, uint64_t character_id, const uint8_t* hmac, size_t hmacSize);

	/// Builds full SHARD_TICKET_RESPONSE packet (Master→Client). \a ticketPayload is the blob from BuildShardTicketPayload.
	std::vector<uint8_t> BuildShardTicketResponsePacket(uint32_t requestId, const std::vector<uint8_t>& ticketPayload);

	/// Builds SHARD_TICKET_ACCEPTED packet (Shard→Client). requestId from PRESENT_SHARD_TICKET or 0.
	std::vector<uint8_t> BuildShardTicketAcceptedPacket(uint32_t requestId);

	/// Builds SHARD_TICKET_REJECTED packet (Shard→Client). requestId from PRESENT_SHARD_TICKET or 0; optional \a reason.
	std::vector<uint8_t> BuildShardTicketRejectedPacket(uint32_t requestId, std::string_view reason = {});

	/// Builds PRESENT_SHARD_TICKET packet (Client→Shard). \a ticketPayload is the blob from SHARD_TICKET_RESPONSE.
	std::vector<uint8_t> BuildPresentShardTicketPacket(uint32_t requestId, std::span<const uint8_t> ticketPayload);
}
