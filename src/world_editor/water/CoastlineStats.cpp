#include "src/world_editor/water/CoastlineStats.h"

#include <cmath>

namespace engine::editor::world
{
	CoastlineStats ComputeCoastlineStats(
		const ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		float beachLandBandMeters,
		float beachSeaBandMeters,
		std::span<const CoastlineSegment> segments)
	{
		CoastlineStats stats;
		const int W = grid.width;
		const int H = grid.height;
		if (W < 1 || H < 1) return stats;

		const float seaLow  = seaLevelMeters - beachSeaBandMeters;
		const float seaHigh = seaLevelMeters + beachLandBandMeters;

		for (int z = 0; z < H; ++z)
		{
			for (int x = 0; x < W; ++x)
			{
				const float h = grid.Get(x, z);
				if (h > seaLevelMeters) stats.landCells++;
				else                    stats.oceanCells++;
				if (h >= seaLow && h <= seaHigh) stats.beachBandCells++;
			}
		}

		float lenSum = 0.0f;
		for (const auto& s : segments)
		{
			const float dx = s.bx - s.ax;
			const float dz = s.bz - s.az;
			lenSum += std::sqrt(dx * dx + dz * dz);
		}
		stats.coastlineLengthMeters = lenSum;
		return stats;
	}
}
