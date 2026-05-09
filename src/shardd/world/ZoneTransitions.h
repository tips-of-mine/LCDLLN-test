#pragma once

#include "engine/server/ServerProtocol.h"

#include <cstdint>
#include <vector>

namespace engine::server
{
	/// Axis-aligned transition trigger volume in zone-local XZ coordinates.
	struct TransitionVolume
	{
		uint32_t sourceZoneId = 0;
		uint32_t targetZoneId = 0;
		float minX = 0.0f;
		float minZ = 0.0f;
		float maxX = 0.0f;
		float maxZ = 0.0f;
		float spawnX = 0.0f;
		float spawnY = 0.0f;
	};

	/// Server-side transition table used to validate zone changes.
	class ZoneTransitionMap final
	{
	public:
		/// Construct an empty transition table.
		ZoneTransitionMap() = default;

		/// Release all transition volumes.
		~ZoneTransitionMap();

		/// Create the minimal authoritative transition volumes required by M13.4.
		bool Init();

		/// Release all transition volumes and logs shutdown.
		void Shutdown();

		/// Validate a zone transition for a player position and compute its spawn point.
		bool ResolveTransition(uint32_t sourceZoneId, float positionX, float positionZ, ZoneChangeMessage& outMessage) const;

	private:
		std::vector<TransitionVolume> m_volumes;
		bool m_initialized = false;
	};
}
