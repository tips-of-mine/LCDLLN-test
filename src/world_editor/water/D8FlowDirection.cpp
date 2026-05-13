#include "src/world_editor/water/D8FlowDirection.h"

#include <cmath>
#include <cstddef>

namespace engine::editor::world
{
	std::vector<uint8_t> ComputeD8FlowDirection(const ConsolidatedHeightGrid& grid)
	{
		const int W = grid.width;
		const int H = grid.height;
		std::vector<uint8_t> dirs(static_cast<size_t>(W) * static_cast<size_t>(H),
			kSinkDir);
		if (W < 2 || H < 2) return dirs;

		// Pré-calcule des distances pour chaque offset (en mètres). Le tableau
		// est const, hoist hors boucle pour éviter le sqrt par cellule.
		float distMeters[8];
		for (int i = 0; i < 8; ++i)
		{
			const float dx = static_cast<float>(kD8Order[i].dx);
			const float dz = static_cast<float>(kD8Order[i].dz);
			distMeters[i] = std::sqrt(dx * dx + dz * dz) * grid.cellSizeMeters;
		}

		for (int z = 0; z < H; ++z)
		{
			for (int x = 0; x < W; ++x)
			{
				const float h_self = grid.Get(x, z);
				float bestSlope = 0.0f;
				uint8_t bestDir = kSinkDir;

				for (uint8_t i = 0; i < 8u; ++i)
				{
					const int nx = x + kD8Order[i].dx;
					const int nz = z + kD8Order[i].dz;
					if (nx < 0 || nx >= W || nz < 0 || nz >= H) continue;
					const float h_n   = grid.Get(nx, nz);
					const float delta = h_self - h_n;
					if (delta <= 0.0f) continue;     // pas descendant
					const float slope = delta / distMeters[i];
					if (slope > bestSlope)
					{
						bestSlope = slope;
						bestDir   = i;
					}
				}
				dirs[static_cast<size_t>(z) * W + x] = bestDir;
			}
		}
		return dirs;
	}
}
