#pragma once

namespace engine::core { class Config; }

namespace engine::world
{
	/// LOD level (0 = highest detail, 3 = lowest). Base distances: LOD0 0-25m, LOD1 25-60m, LOD2 60-150m, LOD3 150-400m (M09.3).
	constexpr int kLodLevelCount = 4;

	/// Reads LOD distance thresholds from config and provides LOD selection by distance (M09.3).
	/// Config keys: lod.distance_lod0_max (25), lod.distance_lod1_max (60), lod.distance_lod2_max (150), lod.distance_lod3_max (400).
	class LodConfig
	{
	public:
		LodConfig() = default;

		/// Load distance thresholds from config (meters). Defaults: 25, 60, 150, 400.
		void Init(const engine::core::Config& config);

		/// Returns LOD level 0..3 for the given distance from camera (meters).
		/// LOD0: [0, d0], LOD1: (d0, d1], LOD2: (d1, d2], LOD3: (d2, d3], beyond d3: LOD3.
		int GetLodLevel(float distanceMeters) const;

		/// Returns the max distance (meters) for the given LOD level (0..3).
		float GetDistanceMax(int lodLevel) const;

	private:
		float m_distanceMax[kLodLevelCount] = { 25.0f, 60.0f, 150.0f, 400.0f };
	};
}
