#include "engine/client/ZonePreloadHook.h"

#include "engine/core/Log.h"
#include "engine/world/StreamCache.h"
#include "engine/world/StreamingScheduler.h"
#include "engine/world/WorldModel.h"

namespace engine::client
{
	ZonePreloadHook::~ZonePreloadHook()
	{
		Shutdown();
	}

	bool ZonePreloadHook::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ZonePreloadHook] Init ignored: already initialized");
			return true;
		}

		m_initialized = true;
		LOG_INFO(Core, "[ZonePreloadHook] Init OK");
		return true;
	}

	void ZonePreloadHook::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		LOG_INFO(Core, "[ZonePreloadHook] Destroyed");
	}

	bool ZonePreloadHook::ApplyZoneChange(
		const engine::server::ZoneChangeMessage& message,
		engine::world::World& world,
		engine::world::StreamingScheduler& scheduler,
		engine::world::StreamCache& streamCache,
		bool& taaHistoryInvalid,
		ZonePreloadRequest& outRequest)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ZonePreloadHook] ApplyZoneChange FAILED: hook not initialized");
			return false;
		}

		streamCache.Clear();
		scheduler.ResetForZoneChange();

		outRequest.zoneId = message.zoneId;
		outRequest.spawnPositionZoneLocal = engine::math::Vec3(message.spawnPositionX, message.spawnPositionY, message.spawnPositionZ);
		outRequest.clearReplicatedEntities = true;
		outRequest.requestSpawns = true;

		world.Update(outRequest.spawnPositionZoneLocal);
		scheduler.PushRequests(world.GetPendingChunkRequests(), outRequest.spawnPositionZoneLocal, engine::math::Vec3(0.0f, 0.0f, 1.0f));
		scheduler.DropStaleFromAllQueues();
		taaHistoryInvalid = true;

		LOG_INFO(Core,
			"[ZonePreloadHook] Zone preload requested (zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}), request_spawns={})",
			outRequest.zoneId,
			outRequest.spawnPositionZoneLocal.x,
			outRequest.spawnPositionZoneLocal.y,
			outRequest.spawnPositionZoneLocal.z,
			outRequest.requestSpawns ? "true" : "false");
		return true;
	}
}
