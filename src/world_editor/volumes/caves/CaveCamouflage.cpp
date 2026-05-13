#include "src/world_editor/volumes/caves/CaveCamouflage.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world::volumes::caves
{
	engine::editor::world::SparseChunkDeltas ComputeCaveSplatWeights(
		const CaveSplatPatch& patch)
	{
		engine::editor::world::SparseChunkDeltas out;
		if (patch.radiusMeters <= 0.0f || patch.strength <= 0.0f) return out;

		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);
		const float chunkSpan =
			(kRes - 1) * engine::world::terrain::kTerrainCellSizeMeters;

		const float minX = patch.worldX - patch.radiusMeters;
		const float maxX = patch.worldX + patch.radiusMeters;
		const float minZ = patch.worldZ - patch.radiusMeters;
		const float maxZ = patch.worldZ + patch.radiusMeters;

		const int chunkXMin = static_cast<int>(std::floor(minX / chunkSpan));
		const int chunkXMax = static_cast<int>(std::floor(maxX / chunkSpan));
		const int chunkZMin = static_cast<int>(std::floor(minZ / chunkSpan));
		const int chunkZMax = static_cast<int>(std::floor(maxZ / chunkSpan));

		const float r2 = patch.radiusMeters * patch.radiusMeters;
		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				const float chunkOX = static_cast<float>(cx) * chunkSpan;
				const float chunkOZ = static_cast<float>(cz) * chunkSpan;
				for (int iz = 0; iz < kRes; ++iz)
				{
					const float wz = chunkOZ + static_cast<float>(iz);
					if (wz < minZ || wz > maxZ) continue;
					for (int ix = 0; ix < kRes; ++ix)
					{
						const float wx = chunkOX + static_cast<float>(ix);
						if (wx < minX || wx > maxX) continue;
						const float dx = wx - patch.worldX;
						const float dz = wz - patch.worldZ;
						const float d2 = dx * dx + dz * dz;
						if (d2 > r2) continue;
						const float d  = std::sqrt(d2);
						const float u  = d / patch.radiusMeters;
						// 1 - smoothstep(0, 1, u) = 1 - u²(3 - 2u)
						const float w  = 1.0f - u * u * (3.0f - 2.0f * u);
						if (w <= 0.0f) continue;
						const engine::world::GlobalChunkCoord coord{ cx, cz };
						const uint32_t idx = static_cast<uint32_t>(iz * kRes + ix);
						out[coord][idx] = std::max(out[coord][idx], w * patch.strength);
					}
				}
			}
		}
		return out;
	}
}
