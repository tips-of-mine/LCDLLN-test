// M100.18 — Implémentation Poisson-disk (Bridson 2007), pure et déterministe.

#include "src/client/world/foliage/PoissonDiskSampler.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace engine::world::foliage
{
	std::vector<engine::math::Vec3> SamplePoissonDisk(float width, float height,
	                                                  float minRadius, uint64_t seed)
	{
		std::vector<engine::math::Vec3> points;
		if (width <= 0.0f || height <= 0.0f || minRadius <= 0.0f) return points;

		constexpr float kPi = 3.14159265358979323846f;
		constexpr int kCandidates = 30;

		const float cell = minRadius / std::sqrt(2.0f);
		const int gw = std::max(1, static_cast<int>(std::ceil(width / cell)));
		const int gh = std::max(1, static_cast<int>(std::ceil(height / cell)));
		std::vector<int> grid(static_cast<size_t>(gw) * gh, -1);

		std::mt19937_64 rng(seed);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);

		auto gridIndex = [&](float x, float z) -> int
		{
			int gx = static_cast<int>(x / cell);
			int gz = static_cast<int>(z / cell);
			if (gx < 0) gx = 0; if (gx >= gw) gx = gw - 1;
			if (gz < 0) gz = 0; if (gz >= gh) gz = gh - 1;
			return gx + gz * gw;
		};

		auto fits = [&](float x, float z) -> bool
		{
			if (x < 0.0f || x >= width || z < 0.0f || z >= height) return false;
			const int gx = static_cast<int>(x / cell);
			const int gz = static_cast<int>(z / cell);
			const float r2 = minRadius * minRadius;
			for (int dz = -2; dz <= 2; ++dz)
			{
				for (int dx = -2; dx <= 2; ++dx)
				{
					const int nx = gx + dx, nz = gz + dz;
					if (nx < 0 || nx >= gw || nz < 0 || nz >= gh) continue;
					const int idx = grid[static_cast<size_t>(nx) + static_cast<size_t>(nz) * gw];
					if (idx < 0) continue;
					const auto& p = points[static_cast<size_t>(idx)];
					const float ddx = p.x - x, ddz = p.z - z;
					if (ddx * ddx + ddz * ddz < r2) return false;
				}
			}
			return true;
		};

		auto addPoint = [&](float x, float z)
		{
			const int index = static_cast<int>(points.size());
			points.push_back(engine::math::Vec3(x, 0.0f, z));
			grid[static_cast<size_t>(gridIndex(x, z))] = index;
		};

		std::vector<int> active;
		// Point initial.
		addPoint(u01(rng) * width, u01(rng) * height);
		active.push_back(0);

		while (!active.empty())
		{
			const int activeIdx = static_cast<int>(u01(rng) * static_cast<float>(active.size()));
			const int pointIdx = active[static_cast<size_t>(activeIdx)];
			const engine::math::Vec3 base = points[static_cast<size_t>(pointIdx)];
			bool found = false;
			for (int i = 0; i < kCandidates; ++i)
			{
				const float ang = 2.0f * kPi * u01(rng);
				const float rad = minRadius * (1.0f + u01(rng)); // annulus [r, 2r]
				const float cx = base.x + std::cos(ang) * rad;
				const float cz = base.z + std::sin(ang) * rad;
				if (fits(cx, cz))
				{
					addPoint(cx, cz);
					active.push_back(static_cast<int>(points.size()) - 1);
					found = true;
					break;
				}
			}
			if (!found)
			{
				active[static_cast<size_t>(activeIdx)] = active.back();
				active.pop_back();
			}
		}
		return points;
	}
}
