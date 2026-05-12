// src/client/world/water/WaterSampler.cpp
#include "src/client/world/water/WaterSampler.h"

namespace engine::world::water
{
	namespace
	{
		// Point-in-polygon 2D (algorithme du nombre d'intersections, XZ).
		// Polygone CCW garanti par M100.13.
		bool PointInPolygonXZ(float px, float pz,
			const std::vector<engine::math::Vec3>& poly) noexcept
		{
			const size_t n = poly.size();
			if (n < 3) return false;
			bool inside = false;
			for (size_t i = 0, j = n - 1; i < n; j = i++)
			{
				const float xi = poly[i].x, zi = poly[i].z;
				const float xj = poly[j].x, zj = poly[j].z;
				const bool intersect = ((zi > pz) != (zj > pz)) &&
					(px < (xj - xi) * (pz - zi) / (zj - zi) + xi);
				if (intersect) inside = !inside;
			}
			return inside;
		}
	}

	bool WaterSampler::Init(const WaterScene& scene) noexcept
	{
		m_scene = &scene;
		return true;
	}

	std::optional<WaterSample> WaterSampler::Sample(engine::math::Vec3 worldPos) const noexcept
	{
		if (!m_scene) return std::nullopt;

		for (const auto& lake : m_scene->lakes)
		{
			if (PointInPolygonXZ(worldPos.x, worldPos.z, lake.polygon))
			{
				const float depth = lake.waterLevelY - worldPos.y;
				if (depth > 0.0f)
				{
					return WaterSample{ lake.waterLevelY, depth };
				}
			}
		}
		return std::nullopt;
	}
}
