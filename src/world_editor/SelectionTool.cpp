// M100.34 — Implémentation SelectionTool (géométrie pure).

#include "src/world_editor/SelectionTool.h"

#include <utility>

namespace engine::editor::world
{
	void SelectionRect::Normalize()
	{
		if (minX > maxX) std::swap(minX, maxX);
		if (minZ > maxZ) std::swap(minZ, maxZ);
	}

	bool SelectionRect::Contains(float x, float z) const
	{
		return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
	}

	std::vector<uint32_t> SelectInRect(const std::vector<SelectablePoint>& points, SelectionRect rect)
	{
		rect.Normalize();
		std::vector<uint32_t> out;
		for (const auto& p : points)
		{
			if (rect.Contains(p.x, p.z))
				out.push_back(p.id);
		}
		return out;
	}

	namespace
	{
		/// Test d'inclusion d'un point dans un polygone par ray-casting horizontal
		/// (règle pair/impair). `poly` : sommets {x,z}.
		bool PointInPolygon(float x, float z, const std::vector<std::pair<float, float>>& poly)
		{
			bool inside = false;
			const size_t n = poly.size();
			for (size_t i = 0, j = n - 1; i < n; j = i++)
			{
				const float xi = poly[i].first,  zi = poly[i].second;
				const float xj = poly[j].first,  zj = poly[j].second;
				// L'arête (j→i) croise-t-elle la demi-droite horizontale partant
				// de (x,z) vers +X ? Comparaison sur Z, intersection sur X.
				const bool straddles = (zi > z) != (zj > z);
				if (straddles)
				{
					const float xCross = xj + (z - zj) / (zi - zj) * (xi - xj);
					if (x < xCross)
						inside = !inside;
				}
			}
			return inside;
		}
	}

	std::vector<uint32_t> SelectInLasso(const std::vector<SelectablePoint>& points,
		const std::vector<std::pair<float, float>>& polygon)
	{
		std::vector<uint32_t> out;
		if (polygon.size() < 3) return out;
		for (const auto& p : points)
		{
			if (PointInPolygon(p.x, p.z, polygon))
				out.push_back(p.id);
		}
		return out;
	}
}
