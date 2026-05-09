#include "engine/server/LagCompensation.h"

#include "engine/core/Log.h"

#include <cmath>

namespace engine::server
{

// ===========================================================================
// EntityPositionHistory
// ===========================================================================

void EntityPositionHistory::Record(uint64_t timestampMs, float posX, float posZ)
{
	m_samples[m_head] = EntityPositionSample{ timestampMs, posX, posZ };
	m_head = (m_head + 1) % kMaxSamples;
	if (m_count < kMaxSamples)
		++m_count;
}

size_t EntityPositionHistory::OldestIndex(size_t i) const
{
	// When the buffer is full, m_head points to the oldest slot.
	// When not yet full, slot 0 is the oldest.
	const size_t oldest = (m_count == kMaxSamples) ? m_head : 0u;
	return (oldest + i) % kMaxSamples;
}

bool EntityPositionHistory::QueryAtTime(uint64_t timestampMs,
                                        float& outPosX,
                                        float& outPosZ) const
{
	if (m_count < 2)
		return false; // Need at least two samples to interpolate.

	// Walk samples from oldest to newest; find the bracketing pair.
	for (size_t i = 0; i + 1 < m_count; ++i)
	{
		const EntityPositionSample& a = m_samples[OldestIndex(i)];
		const EntityPositionSample& b = m_samples[OldestIndex(i + 1)];

		if (timestampMs < a.timestampMs)
			return false; // Requested time is before the oldest sample.

		if (timestampMs <= b.timestampMs)
		{
			// Linearly interpolate between a and b.
			const uint64_t span = b.timestampMs - a.timestampMs;
			if (span == 0u)
			{
				// Identical timestamps: return either sample.
				outPosX = a.positionX;
				outPosZ = a.positionZ;
			}
			else
			{
				const float t = static_cast<float>(timestampMs - a.timestampMs)
				                / static_cast<float>(span);
				outPosX = a.positionX + t * (b.positionX - a.positionX);
				outPosZ = a.positionZ + t * (b.positionZ - a.positionZ);
			}
			return true;
		}
	}

	// Requested time is past the newest sample — clamp to newest.
	const EntityPositionSample& newest = m_samples[OldestIndex(m_count - 1)];
	if (timestampMs == newest.timestampMs)
	{
		outPosX = newest.positionX;
		outPosZ = newest.positionZ;
		return true;
	}

	return false; // Requested time is in the future (no data available).
}

// ===========================================================================
// LagCompensationManager — constructors / destructor
// ===========================================================================

LagCompensationManager::LagCompensationManager()
{
	LOG_INFO(Core, "[LagCompensation] Default ctor (maxRewindMs={})", m_cfg.maxRewindMs);
}

LagCompensationManager::LagCompensationManager(const Config& cfg)
    : m_cfg(cfg)
{
	LOG_INFO(Core, "[LagCompensation] Ctor OK (maxRewindMs={})", m_cfg.maxRewindMs);
}

LagCompensationManager::~LagCompensationManager()
{
	Shutdown();
}

// ===========================================================================
// LagCompensationManager — Init / Shutdown
// ===========================================================================

bool LagCompensationManager::Init()
{
	if (m_initialized)
	{
		LOG_WARN(Core, "[LagCompensation] Init called while already initialized — ignored");
		return false;
	}

	if (m_cfg.maxRewindMs <= 0.0f)
	{
		LOG_ERROR(Core, "[LagCompensation] Init FAILED: maxRewindMs must be > 0 (got {})",
		          m_cfg.maxRewindMs);
		return false;
	}

	m_positionHistories.clear();
	m_initialized = true;

	LOG_INFO(Core, "[LagCompensation] Init OK (maxRewindMs={} historyCapPerEntity={})",
	         m_cfg.maxRewindMs, EntityPositionHistory::kMaxSamples);
	return true;
}

void LagCompensationManager::Shutdown()
{
	if (!m_initialized)
		return;

	const size_t trackedCount = m_positionHistories.size();
	m_positionHistories.clear();
	m_initialized = false;

	LOG_INFO(Core, "[LagCompensation] Shutdown (entitiesPurged={})", trackedCount);
}

// ===========================================================================
// LagCompensationManager — position recording
// ===========================================================================

void LagCompensationManager::RecordEntityPosition(EntityId entityId,
                                                   uint64_t timestampMs,
                                                   float posX,
                                                   float posZ)
{
	if (!m_initialized)
	{
		LOG_WARN(Core, "[LagCompensation] RecordEntityPosition called before Init — ignored");
		return;
	}

	m_positionHistories[entityId].Record(timestampMs, posX, posZ);

	LOG_TRACE(Core, "[LagCompensation] Record entity={} t={}ms pos=({:.2f},{:.2f})",
	          entityId, timestampMs, posX, posZ);
}

void LagCompensationManager::RemoveEntity(EntityId entityId)
{
	if (!m_initialized)
		return;

	const auto erased = m_positionHistories.erase(entityId);
	if (erased > 0)
	{
		LOG_DEBUG(Core, "[LagCompensation] RemoveEntity entity={} history purged", entityId);
	}
	else
	{
		LOG_WARN(Core, "[LagCompensation] RemoveEntity entity={} not found in history", entityId);
	}
}

// ===========================================================================
// LagCompensationManager — hit validation
// ===========================================================================

HitValidationResult LagCompensationManager::ValidateHit(const LagCompSkillRequest& request,
                                                         uint64_t serverNowMs,
                                                         uint32_t estimatedRttMs) const
{
	HitValidationResult result;
	result.targetEntityId = request.targetEntityId;

	if (!m_initialized)
	{
		result.rejectReason = "LagCompensationManager not initialized";
		LOG_WARN(Core, "[LagCompensation] ValidateHit called before Init — rejected");
		return result;
	}

	// 1. Compute the half-RTT rewind offset.
	const uint64_t halfRttMs = static_cast<uint64_t>(estimatedRttMs) / 2u;

	// Guard against client timestamp that is in the future.
	const uint64_t clientTs = request.clientTimestampMs;

	// 2. Compute rewind delta = how far back in time from server-now we want to look.
	//    rewindDelta = serverNowMs - (clientTs - halfRtt)
	//    i.e. clientTs - halfRtt is the "moment the client perceived the hit".
	uint64_t rewindDelta = 0u;
	if (clientTs >= halfRttMs && (clientTs - halfRttMs) <= serverNowMs)
	{
		rewindDelta = serverNowMs - (clientTs - halfRttMs);
	}
	else
	{
		// Fallback: clamp to serverNow (no rewind).
		rewindDelta = 0u;
		LOG_WARN(Core,
		         "[LagCompensation] ValidateHit entity={}->{}: clientTs={} halfRtt={} out of"
		         " range — rewindDelta clamped to 0",
		         request.casterEntityId, request.targetEntityId,
		         clientTs, halfRttMs);
	}

	// 3. Clamp rewind delta to anti-abuse cap.
	const uint64_t maxRewindMsU = static_cast<uint64_t>(m_cfg.maxRewindMs);
	if (rewindDelta > maxRewindMsU)
	{
		LOG_WARN(Core,
		         "[LagCompensation] ValidateHit entity={}->{}: rewindDelta={}ms clamped to {}ms"
		         " (anti-abuse cap)",
		         request.casterEntityId, request.targetEntityId,
		         rewindDelta, maxRewindMsU);
		rewindDelta = maxRewindMsU;
	}

	const uint64_t rewindTime = serverNowMs - rewindDelta;
	result.rewindTimestampMs  = rewindTime;

	LOG_DEBUG(Core,
	          "[LagCompensation] ValidateHit entity={}->{} skill='{}' rewindTime={}ms"
	          " (delta={}ms rtt={}ms)",
	          request.casterEntityId, request.targetEntityId,
	          request.skillId, rewindTime, rewindDelta, estimatedRttMs);

	// 4. Fetch caster rewound position.
	const auto casterIt = m_positionHistories.find(request.casterEntityId);
	if (casterIt == m_positionHistories.end())
	{
		result.rejectReason = "caster position history not found";
		LOG_WARN(Core, "[LagCompensation] ValidateHit: caster entity={} has no history — rejected",
		         request.casterEntityId);
		return result;
	}

	float casterX = 0.0f;
	float casterZ = 0.0f;
	if (!casterIt->second.QueryAtTime(rewindTime, casterX, casterZ))
	{
		result.rejectReason = "caster position not available at rewind time";
		LOG_WARN(Core,
		         "[LagCompensation] ValidateHit: caster entity={} history does not cover"
		         " rewindTime={}ms — rejected",
		         request.casterEntityId, rewindTime);
		return result;
	}

	// 5. Fetch target rewound position.
	const auto targetIt = m_positionHistories.find(request.targetEntityId);
	if (targetIt == m_positionHistories.end())
	{
		result.rejectReason = "target position history not found";
		LOG_WARN(Core, "[LagCompensation] ValidateHit: target entity={} has no history — rejected",
		         request.targetEntityId);
		return result;
	}

	float targetX = 0.0f;
	float targetZ = 0.0f;
	if (!targetIt->second.QueryAtTime(rewindTime, targetX, targetZ))
	{
		result.rejectReason = "target position not available at rewind time";
		LOG_WARN(Core,
		         "[LagCompensation] ValidateHit: target entity={} history does not cover"
		         " rewindTime={}ms — rejected",
		         request.targetEntityId, rewindTime);
		return result;
	}

	// 6. Compute XZ distance at rewound positions.
	const float dx = targetX - casterX;
	const float dz = targetZ - casterZ;
	const float distance = std::sqrt(dx * dx + dz * dz);
	result.rewindedDistanceMeters = distance;

	if (distance > request.skillRangeMeters)
	{
		result.rejectReason = "out of range at rewound positions";
		LOG_DEBUG(Core,
		          "[LagCompensation] ValidateHit: entity={}->{} distance={:.2f}m > range={:.2f}m"
		          " — rejected (out of range)",
		          request.casterEntityId, request.targetEntityId,
		          distance, request.skillRangeMeters);
		return result;
	}

	// 7. Validate line-of-sight (provided by caller; rewound-geometry LOS is out of M30.3 scope).
	if (!request.hasLineOfSight)
	{
		result.rejectReason = "no line of sight";
		LOG_DEBUG(Core,
		          "[LagCompensation] ValidateHit: entity={}->{} LOS check failed — rejected",
		          request.casterEntityId, request.targetEntityId);
		return result;
	}

	// 8. Hit is valid.
	result.valid = true;

	LOG_INFO(Core,
	         "[LagCompensation] HIT VALID entity={}->{} skill='{}' dist={:.2f}m"
	         " range={:.2f}m rewindTime={}ms",
	         request.casterEntityId, request.targetEntityId,
	         request.skillId, distance, request.skillRangeMeters, rewindTime);

	return result;
}

} // namespace engine::server
