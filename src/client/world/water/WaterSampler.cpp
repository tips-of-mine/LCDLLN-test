// src/client/world/water/WaterSampler.cpp
#include "src/client/world/water/WaterSampler.h"

#include <cmath>

namespace engine::world::water
{
	namespace
	{
		// Projection orthogonale 2D (XZ) de `p` sur le segment [a, b].
		// Retourne `t` clampé dans [0, 1] et la distance latérale.
		struct SegmentProjection
		{
			float t = 0.0f;
			float distXZ = 0.0f;
		};

		SegmentProjection ProjectOnSegmentXZ(const engine::math::Vec3& p,
			const engine::math::Vec3& a, const engine::math::Vec3& b) noexcept
		{
			const float dx = b.x - a.x;
			const float dz = b.z - a.z;
			const float lenSq = dx * dx + dz * dz;
			if (lenSq < 1e-12f) {
				const float ddx = p.x - a.x;
				const float ddz = p.z - a.z;
				return { 0.0f, std::sqrt(ddx * ddx + ddz * ddz) };
			}
			float t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / lenSq;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			const float projX = a.x + t * dx;
			const float projZ = a.z + t * dz;
			const float ddx = p.x - projX;
			const float ddz = p.z - projZ;
			return { t, std::sqrt(ddx * ddx + ddz * ddz) };
		}

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
		for (const auto& river : m_scene->rivers)
		{
			for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
			{
				const auto& na = river.nodes[i];
				const auto& nb = river.nodes[i + 1];
				const auto proj = ProjectOnSegmentXZ(worldPos, na.position, nb.position);
				const float widthLocal = na.widthMeters * (1.0f - proj.t) + nb.widthMeters * proj.t;
				if (proj.distXZ <= widthLocal * 0.5f)
				{
					const float surfaceY = na.position.y * (1.0f - proj.t) + nb.position.y * proj.t;
					const float depth = surfaceY - worldPos.y;
					if (depth > 0.0f)
					{
						return WaterSample{ surfaceY, depth };
					}
				}
			}
		}
		return std::nullopt;
	}
}
