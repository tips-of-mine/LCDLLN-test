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

	/// Password reset flow (M33.2).
	/// Client→Master: request reset link for email; Master→Client: ack (always success to avoid enumeration).
	constexpr uint16_t kOpcodeForgotPasswordRequest = 21u;
	constexpr uint16_t kOpcodeForgotPasswordResponse = 22u;
	/// Client→Master: submit reset token + new client_hash; Master→Client: success or error.
	constexpr uint16_t kOpcodeResetPasswordRequest = 23u;
	constexpr uint16_t kOpcodeResetPasswordResponse = 24u;
	/// Email verification flow (M33.2).
	/// Client→Master: submit account_id + 6-digit code; Master→Client: success or error.
	constexpr uint16_t kOpcodeVerifyEmailRequest = 25u;
	constexpr uint16_t kOpcodeVerifyEmailResponse = 26u;
}
