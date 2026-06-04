// M100.28 — Implémentation des requêtes de zone (pures).

#include "src/client/world/zones/ZoneQuery.h"

#include <algorithm>
#include <cmath>

namespace engine::world::zones
{
	namespace
	{
		bool RayCastInside(const std::vector<engine::math::Vec3>& poly, float x, float z)
		{
			if (poly.size() < 3) return false;
			bool inside = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				const float xi = poly[i].x, zi = poly[i].z;
				const float xj = poly[j].x, zj = poly[j].z;
				const bool intersect = ((zi > z) != (zj > z)) &&
					(x < (xj - xi) * (z - zi) / (zj - zi + 1e-9f) + xi);
				if (intersect) inside = !inside;
			}
			return inside;
		}

		float DistToSegment(float px, float pz, float ax, float az, float bx, float bz)
		{
			const float dx = bx - ax, dz = bz - az;
			const float len2 = dx * dx + dz * dz;
			float t = (len2 > 1e-9f) ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
			t = std::max(0.0f, std::min(1.0f, t));
			const float cx = ax + t * dx, cz = az + t * dz;
			const float ex = px - cx, ez = pz - cz;
			return std::sqrt(ex * ex + ez * ez);
		}
	} // namespace

	bool PointInZone(const GameplayZone& zone, float x, float z)
	{
		return RayCastInside(zone.polygon, x, z);
	}

	float SignedDistanceToPolygon(const std::vector<engine::math::Vec3>& poly, float x, float z)
	{
		if (poly.size() < 3) return -1.0e9f;
		float minDist = 1.0e9f;
		for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			minDist = std::min(minDist, DistToSegment(x, z, poly[j].x, poly[j].z, poly[i].x, poly[i].z));
		return RayCastInside(poly, x, z) ? minDist : -minDist;
	}

	float WeatherOverrideBlend(const GameplayZone& zone, float x, float z)
	{
		const float d = SignedDistanceToPolygon(zone.polygon, x, z);
		if (d <= 0.0f) return 0.0f; // dehors ou sur le bord
		const float margin = (zone.transitionMarginMeters > 1e-5f) ? zone.transitionMarginMeters : 1e-5f;
		return std::min(1.0f, d / margin);
	}
}
