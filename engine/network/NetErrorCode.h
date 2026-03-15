#pragma once

#include <cstdint>

namespace engine::network
{
	/// Centralized protocol v1 error codes. Stable: do not reassign values without bumping protocol doc version.
	/// See tickets/docs/protocol_v1.md for reaction rules (ERROR vs disconnect).
	enum class NetErrorCode : uint32_t
	{
		OK = 0,
		// Protocol / framing (97–99)
		PACKET_OVERSIZE = 97,
		UNKNOWN_OPCODE = 98,
		INVALID_PACKET = 99,
		// Request / auth (100–104)
		BAD_REQUEST = 100,
		INVALID_CREDENTIALS = 101,
		ACCOUNT_LOCKED = 102,
		ACCOUNT_NOT_FOUND = 103,
		ALREADY_LOGGED_IN = 104,
		// Registration (200–202)
		REGISTRATION_DISABLED = 200,
		REGISTRATION_INVALID = 201,
		LOGIN_ALREADY_TAKEN = 202,
		// Server list (300)
		SERVER_LIST_UNAVAILABLE = 300,
		// Internal / timeout (500–501)
		INTERNAL_ERROR = 500,
		TIMEOUT = 501,
	};
}
