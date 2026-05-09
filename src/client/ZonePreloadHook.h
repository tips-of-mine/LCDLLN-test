#pragma once

#include "engine/math/Math.h"
#include "engine/server/ServerProtocol.h"

namespace engine::world
{
	class StreamCache;
	class StreamingScheduler;
	struct World;
}

namespace engine::client
{
	/// Resulting client-side state update requested by a server-authoritative zone change.
	struct ZonePreloadRequest
	{
		uint32_t zoneId = 0;
		engine::math::Vec3 spawnPositionZoneLocal{};
		bool clearReplicatedEntities = false;
		bool requestSpawns = false;
	};

	/// Minimal client preload hook used when a server-authoritative zone change is received.
	class ZonePreloadHook final
	{
	public:
		/// Construct an uninitialized preload hook.
		ZonePreloadHook() = default;

		/// Release preload hook resources.
		~ZonePreloadHook();

		/// Initialize the preload hook.
		bool Init();

		/// Shutdown the preload hook.
		void Shutdown();

		/// Clear current streaming state, request preload for the new zone and invalidate TAA history.
		bool ApplyZoneChange(
			const engine::server::ZoneChangeMessage& message,
			engine::world::World& world,
			engine::world::StreamingScheduler& scheduler,
			engine::world::StreamCache& streamCache,
			bool& taaHistoryInvalid,
			ZonePreloadRequest& outRequest);

	private:
		bool m_initialized = false;
	};
}
