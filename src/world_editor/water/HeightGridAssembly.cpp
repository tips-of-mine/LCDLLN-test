#include "src/world_editor/water/HeightGridAssembly.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"

namespace engine::editor::world
{
	ConsolidatedHeightGrid BuildGridFromLoadedChunks(TerrainDocument& terrain,
		const engine::core::Config& cfg)
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);
		constexpr int kChunksDim = 2;

		ConsolidatedHeightGrid grid;
		grid.cellSizeMeters = engine::world::terrain::kTerrainCellSizeMeters;
		grid.originCellX    = 0;
		grid.originCellZ    = 0;
		const int W = kChunksDim * (kRes - 1) + 1;
		const int H = kChunksDim * (kRes - 1) + 1;
		grid.width  = W;
		grid.height = H;
		grid.heights.assign(static_cast<size_t>(W) * H, 0.0f);

		for (int cz = 0; cz < kChunksDim; ++cz)
		{
			for (int cx = 0; cx < kChunksDim; ++cx)
			{
				auto chunk = terrain.EnsureLoaded(cfg, cx, cz);
				if (!chunk) continue;
				const int baseX = cx * (kRes - 1);
				const int baseZ = cz * (kRes - 1);
				for (int iz = 0; iz < kRes; ++iz)
				{
					for (int ix = 0; ix < kRes; ++ix)
					{
						const int gx = baseX + ix;
						const int gz = baseZ + iz;
						if (gx >= W || gz >= H) continue;
						grid.heights[static_cast<size_t>(gz) * W + gx] =
							chunk->heights[static_cast<size_t>(iz) * kRes + ix];
					}
				}
			}
		}
		return grid;
	}
}
