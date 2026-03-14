#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::network
{
	/// Protocol v1 constants (see tickets/docs/protocol_v1.md).
	constexpr uint16_t kProtocolV1HeaderSize = 18u;
	constexpr uint32_t kProtocolV1MaxPacketSize = 16384u;
	constexpr uint32_t kProtocolV1MaxStringLength = 8192u;
}
