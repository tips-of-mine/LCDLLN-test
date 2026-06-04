// M100.27 — Implémentation du calcul de shade map (pur, déterministe).

#include "src/client/world/thermal/ShadeMapBuilder.h"

#include <algorithm>
#include <random>

namespace engine::world::thermal
{
	ShadeMap BuildShadeMap(float chunkOriginX, float chunkOriginZ, float chunkSizeMeters,
	                       const CanopySampler& sampler, uint64_t seed)
	{
		ShadeMap map;
		map.resolution = kShadeMapResolution;
		map.coverage.assign(static_cast<size_t>(map.resolution) * map.resolution, 0u);
		if (!sampler || chunkSizeMeters <= 0.0f) return map;

		const float cell = chunkSizeMeters / static_cast<float>(map.resolution);
		std::mt19937_64 rng(seed);
		std::uniform_real_distribution<float> jit(-0.5f, 0.5f);
		constexpr int kSamples = 16;

		for (uint32_t cy = 0; cy < map.resolution; ++cy)
		{
			for (uint32_t cx = 0; cx < map.resolution; ++cx)
			{
				const float baseX = chunkOriginX + (static_cast<float>(cx) + 0.5f) * cell;
				const float baseZ = chunkOriginZ + (static_cast<float>(cy) + 0.5f) * cell;
				int hits = 0;
				for (int s = 0; s < kSamples; ++s)
				{
					const float sx = baseX + jit(rng) * cell;
					const float sz = baseZ + jit(rng) * cell;
					if (sampler(sx, sz)) ++hits;
				}
				const int shade = std::min(255, hits * 16);
				map.coverage[static_cast<size_t>(cy) * map.resolution + cx] = static_cast<uint8_t>(shade);
			}
		}
		return map;
	}
}
