#include "engine/server/ZoneTransitions.h"

#include "engine/core/Log.h"
#include "engine/server/SpatialPartition.h"

#include <algorithm>

namespace engine::server
{
	namespace
	{
		/// Spawn offset used to place the player safely inside the target zone after transition.
		inline constexpr float kSpawnInsetMeters = 128.0f;

		/// Return true when the point lies inside the transition volume.
		bool ContainsPoint(const TransitionVolume& volume, float positionX, float positionZ)
		{
			return positionX >= volume.minX && positionX <= volume.maxX
				&& positionZ >= volume.minZ && positionZ <= volume.maxZ;
		}
	}

	ZoneTransitionMap::~ZoneTransitionMap()
	{
		Shutdown();
	}

	bool ZoneTransitionMap::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[ZoneTransitionMap] Init ignored: already initialized");
			return true;
		}

		m_volumes.clear();

		TransitionVolume forward{};
		forward.sourceZoneId = 1;
		forward.targetZoneId = 2;
		forward.minX = static_cast<float>(kZoneSizeMeters - kCellSizeMeters);
		forward.minZ = 0.0f;
		forward.maxX = static_cast<float>(kZoneSizeMeters);
		forward.maxZ = static_cast<float>(kZoneSizeMeters);
		forward.spawnX = kSpawnInsetMeters;
		forward.spawnY = 0.0f;
		m_volumes.push_back(forward);

		TransitionVolume backward{};
		backward.sourceZoneId = 2;
		backward.targetZoneId = 1;
		backward.minX = 0.0f;
		backward.minZ = 0.0f;
		backward.maxX = static_cast<float>(kCellSizeMeters);
		backward.maxZ = static_cast<float>(kZoneSizeMeters);
		backward.spawnX = static_cast<float>(kZoneSizeMeters) - kSpawnInsetMeters;
		backward.spawnY = 0.0f;
		m_volumes.push_back(backward);

		m_initialized = true;
		LOG_INFO(Net, "[ZoneTransitionMap] Init OK (volumes={})", m_volumes.size());
		return true;
	}

	void ZoneTransitionMap::Shutdown()
	{
		if (!m_initialized && m_volumes.empty())
		{
			return;
		}

		const size_t volumeCount = m_volumes.size();
		m_volumes.clear();
		m_initialized = false;
		LOG_INFO(Net, "[ZoneTransitionMap] Destroyed (volumes={})", volumeCount);
	}

	bool ZoneTransitionMap::ResolveTransition(uint32_t sourceZoneId, float positionX, float positionZ, ZoneChangeMessage& outMessage) const
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[ZoneTransitionMap] ResolveTransition FAILED: table not initialized");
			return false;
		}

		for (const TransitionVolume& volume : m_volumes)
		{
			if (volume.sourceZoneId != sourceZoneId || !ContainsPoint(volume, positionX, positionZ))
			{
				continue;
			}

			outMessage.zoneId = volume.targetZoneId;
			outMessage.spawnPositionX = volume.spawnX;
			outMessage.spawnPositionY = volume.spawnY;
			outMessage.spawnPositionZ = std::clamp(positionZ, kSpawnInsetMeters, static_cast<float>(kZoneSizeMeters) - kSpawnInsetMeters);
			LOG_INFO(Net,
				"[ZoneTransitionMap] Transition resolved (source_zone={}, target_zone={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
				sourceZoneId,
				outMessage.zoneId,
				outMessage.spawnPositionX,
				outMessage.spawnPositionY,
				outMessage.spawnPositionZ);
			return true;
		}

		return false;
	}
}
