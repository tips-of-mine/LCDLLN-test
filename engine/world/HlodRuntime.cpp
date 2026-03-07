#include "engine/world/HlodRuntime.h"
#include "engine/core/Config.h"
#include "engine/math/Frustum.h"
#include "engine/world/WorldModel.h"

#include <cmath>
#include <format>
#include <span>

namespace engine::world
{
	namespace
	{
		constexpr float kChunkAabbMaxY = 256.0f;
	}
	void HlodRuntime::Init(const engine::core::Config& config)
	{
		m_thresholdMeters = static_cast<float>(config.GetDouble("world.hlod_distance_threshold_m", 200.0));
		if (m_thresholdMeters < 0.0f)
			m_thresholdMeters = 200.0f;
	}

	bool HlodRuntime::ShouldUseHlod(float distanceMeters) const
	{
		return distanceMeters > m_thresholdMeters;
	}

	bool HlodRuntime::IsVisible(const engine::math::Frustum& frustum,
	                            const engine::math::Vec3& aabbMin, const engine::math::Vec3& aabbMax,
	                            const engine::math::Vec3& cameraPosition,
	                            float maxDistanceMeters)
	{
		if (!frustum.TestAABB(aabbMin, aabbMax))
			return false;
		if (maxDistanceMeters <= 0.0f)
			return true;
		// Distance from camera to AABB center (stable).
		const float cx = (aabbMin.x + aabbMax.x) * 0.5f;
		const float cy = (aabbMin.y + aabbMax.y) * 0.5f;
		const float cz = (aabbMin.z + aabbMax.z) * 0.5f;
		const float dx = cx - cameraPosition.x;
		const float dy = cy - cameraPosition.y;
		const float dz = cz - cameraPosition.z;
		const float distSq = dx * dx + dy * dy + dz * dz;
		const float maxSq = maxDistanceMeters * maxDistanceMeters;
		return distSq <= maxSq;
	}

	std::string BuildChunkDrawList(
		std::span<const ChunkRequest> requestedChunks,
		const engine::math::Vec3& cameraPosition,
		const engine::math::Frustum& frustum,
		const HlodRuntime& hlod,
		float maxDrawDistanceMeters,
		std::vector<ChunkDrawDecision>& decisionsOut)
	{
		decisionsOut.clear();
		int countHlod = 0, countInst = 0, countCulled = 0;
		for (const ChunkRequest& req : requestedChunks)
		{
			ChunkBounds bounds2d = World::ChunkBounds(req.chunkId);
			engine::math::Vec3 aabbMin(bounds2d.minX, 0.0f, bounds2d.minZ);
			engine::math::Vec3 aabbMax(bounds2d.maxX, kChunkAabbMaxY, bounds2d.maxZ);
			float cx = (aabbMin.x + aabbMax.x) * 0.5f;
			float cy = (aabbMin.y + aabbMax.y) * 0.5f;
			float cz = (aabbMin.z + aabbMax.z) * 0.5f;
			float dx = cx - cameraPosition.x;
			float dy = cy - cameraPosition.y;
			float dz = cz - cameraPosition.z;
			float distanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
			bool visible = IsVisible(frustum, aabbMin, aabbMax, cameraPosition, maxDrawDistanceMeters);
			bool useHlod = hlod.ShouldUseHlod(distanceMeters);
			ChunkDrawDecision dec;
			dec.coord = req.chunkId;
			dec.useHlod = useHlod;
			dec.culled = !visible;
			dec.distanceMeters = distanceMeters;
			decisionsOut.push_back(dec);
			if (dec.culled)
				++countCulled;
			else if (dec.useHlod)
				++countHlod;
			else
				++countInst;
		}
		return std::format("HLOD: {} inst: {} culled: {}", countHlod, countInst, countCulled);
	}
}
