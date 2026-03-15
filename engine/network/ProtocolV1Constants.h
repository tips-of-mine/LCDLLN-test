#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::network
{
	/// Protocol v1 constants (see tickets/docs/protocol_v1.md).
	constexpr uint16_t kProtocolV1HeaderSize = 18u;
	constexpr uint32_t kProtocolV1MaxPacketSize = 16384u;
	constexpr uint32_t kProtocolV1MaxStringLength = 8192u;

	/// Auth/Register opcodes (see tickets/docs/protocol_v1.md).
	constexpr uint16_t kOpcodeAuthRequest = 1u;
	constexpr uint16_t kOpcodeAuthResponse = 2u;
	constexpr uint16_t kOpcodeRegisterRequest = 3u;
	constexpr uint16_t kOpcodeRegisterResponse = 4u;

	/// Heartbeat keep-alive (see tickets/docs/protocol_v1.md).
	constexpr uint16_t kOpcodeHeartbeat = 7u;

	/// Official opcode for ERROR packet (see tickets/docs/protocol_v1.md).
	constexpr uint16_t kOpcodeError = 8u;

	/// Shard↔Master internal opcodes (M22.2).
	constexpr uint16_t kOpcodeShardRegister = 10u;
	constexpr uint16_t kOpcodeShardRegisterOk = 11u;
	constexpr uint16_t kOpcodeShardRegisterError = 12u;
	constexpr uint16_t kOpcodeShardHeartbeat = 13u;

	/// Client→Master shard ticket (M22.4).
	constexpr uint16_t kOpcodeRequestShardTicket = 14u;
	constexpr uint16_t kOpcodeShardTicketResponse = 15u;
	/// Client→Shard present ticket; Shard→Client response.
	constexpr uint16_t kOpcodePresentShardTicket = 16u;
	constexpr uint16_t kOpcodeShardTicketAccepted = 17u;
	constexpr uint16_t kOpcodeShardTicketRejected = 18u;

	/// Client→Master server list (M22.5).
	constexpr uint16_t kOpcodeServerListRequest = 19u;
	constexpr uint16_t kOpcodeServerListResponse = 20u;
}
