#pragma once

#include "engine/math/Math.h"
#include "engine/math/Frustum.h"
#include "engine/world/WorldModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::world
{
	class HlodRuntime;

	/// Per-chunk draw decision: use HLOD vs instances, and culled. M09.5.
	struct ChunkDrawDecision
	{
		ChunkCoord coord{ 0, 0 };
		bool useHlod = false;
		bool culled = false;
		float distanceMeters = 0.0f;
	};

	/// Return type for BuildChunkDrawList (avoids MSVC parse issues with std::string in declaration).
	using HlodDebugString = std::string;
	/// Parameter reference types (avoid MSVC parse issues with qualified names in declarations).
	using Vec3Ref = const engine::math::Vec3&;
	using FrustumRef = const engine::math::Frustum&;

	/// Builds chunk draw list decisions: for each requested chunk, applies frustum + distance culling and HLOD switch by distance.
	/// \param requestedChunks Pointer to chunk requests (from World::GetPendingChunkRequests().data()).
	/// \param requestedChunksCount Number of requests (from World::GetPendingChunkRequests().size()).
	/// \param cameraPosition Zone-local camera position.
	/// \param frustum View frustum for culling.
	/// \param hlod HlodRuntime for threshold and culling.
	/// \param maxDrawDistanceMeters Max distance to draw (0 = no distance cull).
	/// \param decisionsOut Filled with one entry per chunk (useHlod, culled).
	/// \return Debug string for overlay (e.g. "HLOD: 3 inst: 5 culled: 2").
	HlodDebugString BuildChunkDrawList(
		const ChunkRequest* requestedChunks,
		size_t requestedChunksCount,
		Vec3Ref cameraPosition,
		FrustumRef frustum,
		const HlodRuntime& hlod,
		float maxDrawDistanceMeters,
		std::vector<ChunkDrawDecision>& decisionsOut);
	/// HLOD switch threshold and CPU culling (frustum + distance). M09.5.
	/// Base threshold ~200m: beyond that distance use HLOD instead of individual props.
	class HlodRuntime
	{
	public:
		HlodRuntime() = default;

		/// Load HLOD distance threshold from config (world.hlod_distance_threshold_m, default 200).
		void Init(const engine::core::Config& config);

		/// Returns true if we should use HLOD for the given distance from camera (meters).
		/// Beyond threshold we use HLOD; at or below we use individual instances.
		bool ShouldUseHlod(float distanceMeters) const;

		/// Returns the HLOD switch threshold in meters.
		float GetThresholdMeters() const { return m_thresholdMeters; }

		/// CPU culling: returns true if the AABB is visible (inside frustum and within max distance).
		/// Stable: same inputs always give same result (deterministic).
		/// \param maxDistanceMeters Maximum draw distance; 0 = no distance cull.
		static bool IsVisible(const engine::math::Frustum& frustum,
		                      const engine::math::Vec3& aabbMin, const engine::math::Vec3& aabbMax,
		                      const engine::math::Vec3& cameraPosition,
		                      float maxDistanceMeters);

	private:
		float m_thresholdMeters = 200.0f;
	};
}
