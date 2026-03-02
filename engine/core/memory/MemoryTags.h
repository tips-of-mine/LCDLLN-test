#pragma once

#include <cstdint>

namespace engine::core::memory
{
	/// Memory tags used for tracking allocations by subsystem.
	enum class MemTag : uint8_t
	{
		Core = 0,
		Render,
		Assets,
		World,
		Net,
		UI,
		Tools,
		Temp,
		_Count
	};
}

