#include "src/world_editor/water/PathSimplifyDouglasPeucker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace engine::editor::world
{
	namespace
	{
		/// Distance perpendiculaire dans XZ entre `p` et la droite (a, b).
		/// Si a == b, retourne la distance euclidienne XZ de p à a.
		float DistPerpendicularXZ(const engine::math::Vec3& p,
			const engine::math::Vec3& a, const engine::math::Vec3& b)
		{
			const float dx = b.x - a.x;
			const float dz = b.z - a.z;
			const float len2 = dx * dx + dz * dz;
			if (len2 < 1e-12f)
			{
				const float ex = p.x - a.x;
				const float ez = p.z - a.z;
				return std::sqrt(ex * ex + ez * ez);
			}
			// |cross(ab, ap)| / |ab|
			const float ex = p.x - a.x;
			const float ez = p.z - a.z;
			const float cross = std::fabs(dx * ez - dz * ex);
			return cross / std::sqrt(len2);
		}

		/// Marque les indices à conserver via récursion Douglas-Peucker.
		/// `keep` est pré-rempli à `false` ; les extrémités sont marquées par
		/// l'appelant. Implémentation itérative avec une pile pour éviter le
		/// risque de stack overflow sur des polylines très longues.
		void SimplifyRecursiveIter(const std::vector<engine::math::Vec3>& points,
			float tolerance, std::vector<uint8_t>& keep)
		{
			// Pile de paires (loIdx, hiIdx). On traite chaque segment ; si un
			// pivot dépasse la tolérance, on l'ajoute à `keep` et on pousse
			// les deux sous-segments.
			std::vector<std::pair<size_t, size_t>> stack;
			stack.emplace_back(0u, points.size() - 1u);
			while (!stack.empty())
			{
				const auto [lo, hi] = stack.back();
				stack.pop_back();
				if (hi <= lo + 1u) continue;

				float maxDist = 0.0f;
				size_t maxIdx = lo;
				for (size_t i = lo + 1u; i < hi; ++i)
				{
					const float d = DistPerpendicularXZ(points[i],
						points[lo], points[hi]);
					if (d > maxDist)
					{
						maxDist = d;
						maxIdx  = i;
					}
				}
				if (maxDist > tolerance)
				{
					keep[maxIdx] = 1u;
					stack.emplace_back(lo, maxIdx);
					stack.emplace_back(maxIdx, hi);
				}
			}
		}
	}

	std::vector<engine::math::Vec3> SimplifyPolylineDouglasPeucker(
		const std::vector<engine::math::Vec3>& points, float toleranceMeters)
	{
		if (points.size() < 3u || toleranceMeters <= 0.0f) return points;

		std::vector<uint8_t> keep(points.size(), 0u);
		keep.front() = 1u;
		keep.back()  = 1u;
		SimplifyRecursiveIter(points, toleranceMeters, keep);

		std::vector<engine::math::Vec3> simplified;
		simplified.reserve(points.size());
		for (size_t i = 0; i < points.size(); ++i)
		{
			if (keep[i]) simplified.push_back(points[i]);
		}
		return simplified;
	}
}
